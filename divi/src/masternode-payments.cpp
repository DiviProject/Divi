// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"

#include "BlockDiskAccessor.h"
#include <chain.h>
#include "chainparamsbase.h"
#include <masternode.h>
#include "masternode-sync.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "sync.h"
#include "Logging.h"
#include "utilmoneystr.h"
#include "netfulfilledman.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <numeric>
#include <I_BlockSubsidyProvider.h>
#include <script/standard.h>
#include <base58address.h>
#include <chainparams.h>
#include <version.h>
#include <MasternodePaymentData.h>
#include <MasternodeHelpers.h>
#include <MasternodeNetworkMessageManager.h>
#include <timedata.h>
#include <NodeStateRegistry.h>
#include <Node.h>

const int CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED = 6;
const int CMasternodePayments::MNPAYMENTS_SIGNATURES_TOTAL = 10;
constexpr int MN_WINNER_MINIMUM_AGE = 8000;    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Maximum nCheckNum that we allow for ranking masternodes.
 *  In practice, we only need to check if a node is in the top 10
 *  (or rather, top-20 to give some leeway before punishing nodes
 *  with wrong claims).  */
static constexpr unsigned MAX_RANKING_CHECK_NUM = 20;

/** Number of entries (blocks) we keep in the cache of ranked masternodes.  */
static constexpr unsigned RANKING_CACHE_SIZE = 2500;
/**
 * An entry in the ranking cache.  We use mruset to hold the cache,
 * which means that even though it is conceptually a map, we represent
 * it as a set, i.e. instances of this class are both the key and value
 * in one, and compare based on the key.
 */
namespace
{
struct RankingCacheEntry
{

  using value_type = std::array<uint256, MAX_RANKING_CHECK_NUM>;

  /** The scoring hash this is for, i.e. the key.  */
  uint256 scoringBlockHash;

  /** The list of best masternodes by rank (represented through
   *  their vin prevout hashes).  */
  value_type bestVins;

  RankingCacheEntry() = default;
  RankingCacheEntry(RankingCacheEntry&&) = default;
  RankingCacheEntry(const RankingCacheEntry&) = default;

  void operator=(const RankingCacheEntry&) = delete;

};

bool operator==(const RankingCacheEntry& a, const RankingCacheEntry& b)
{
  return a.scoringBlockHash == b.scoringBlockHash;
}

bool operator<(const RankingCacheEntry& a, const RankingCacheEntry& b)
{
  return a.scoringBlockHash < b.scoringBlockHash;
}

} // anonymous namespace

/**
 * Internal helper class that represents the cache of the best MAX_RANKING_NUM
 * nodes for recent block heights.
 */
class CMasternodePayments::RankingCache
{

private:

  /** The best nodes for the last couple of blocks.  */
  mruset<RankingCacheEntry> entries;

public:

  RankingCache()
    : entries(RANKING_CACHE_SIZE)
  {}

  RankingCache(const RankingCache&) = delete;
  void operator=(const RankingCache&) = delete;

  /** Looks up an entry by scoring hash and returns it, or a null
   *  pointer if there is no matching entry.  */
  const RankingCacheEntry::value_type* Find(const uint256& hash) const
  {
    RankingCacheEntry entry;
    entry.scoringBlockHash = hash;

    auto mit = entries.find(entry);
    if (mit == entries.end())
      return nullptr;

    return &mit->bestVins;
  }

  /** Inserts an entry into the cache.  */
  void Insert(const uint256& hash, const RankingCacheEntry::value_type& bestVins)
  {
    RankingCacheEntry entry;
    entry.scoringBlockHash = hash;
    entry.bestVins = bestVins;

    auto ins = entries.insert(std::move(entry));
    assert(ins.second);
  }

};


/** Object for who's going to get paid on which blocks */

CMasternodePayments::CMasternodePayments(
    CNetFulfilledRequestManager& networkFulfilledRequestManager,
    MasternodePaymentData& paymentData,
    MasternodeNetworkMessageManager& networkMessageManager,
    CMasternodeMan& masternodeManager,
    CMasternodeSync& masternodeSynchronization,
    const CChain& activeChain
    ): rankingCache(new RankingCache)
    , nSyncedFromPeer(0)
    , nLastBlockHeight(0)
    , networkFulfilledRequestManager_(networkFulfilledRequestManager)
    , paymentData_(paymentData)
    , networkMessageManager_(networkMessageManager)
    , masternodeManager_(masternodeManager)
    , masternodeSynchronization_(masternodeSynchronization)
    , activeChain_(activeChain)
    , mapMasternodeBlocks(paymentData_.mapMasternodeBlocks)
    , cs_mapMasternodeBlocks(paymentData_.cs_mapMasternodeBlocks)
{
}
CMasternodePayments::~CMasternodePayments()
{
    rankingCache.reset();
}

bool CMasternodePayments::CanVote(const COutPoint& outMasternode, const uint256& scoringBlockHash) const
{
    return paymentData_.canVote(outMasternode,scoringBlockHash);
}


void CMasternodePayments::FillBlockPayee(const CBlockIndex* pindexPrev, CMutableTransaction& txNew, const CBlockRewards &rewards) const
{
    bool hasPayment = true;
    CScript payee;

    //spork
    uint256 scoringBlockHash;
    if (!GetBlockHashForScoring(scoringBlockHash, pindexPrev, 1)) {
        LogPrint("masternode", "FillBlockPayee - failed to get score hash\n");
        return;
    }
    if (!GetBlockPayee(scoringBlockHash, payee)) {
        // No masternode detected, fall back to our own queue.
        payee = GetNextMasternodePayeeInQueueForPayment(pindexPrev, 1);
        if(payee.empty())
        {
            LogPrint("masternode","CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    if (hasPayment) {
        CAmount masternodePayment = rewards.nMasternodeReward;
        txNew.vout.emplace_back(masternodePayment, payee);

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("masternode","Masternode payment of %s to %s\n", FormatMoney(masternodePayment), address2);
    }
}

bool CMasternodePayments::CheckMasternodeWinnerCandidate(CNode* pfrom, CMasternodePaymentWinner& winner) const
{
    const int chainTipHeight = activeChain_.Height();
    if (paymentData_.getPaymentWinnerForHash(winner.GetHash()) != nullptr) {
        LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash(), chainTipHeight);
        masternodeSynchronization_.RecordMasternodeWinnerUpdate(winner.GetHash());
        return false;
    }

    int nFirstBlock = chainTipHeight - CMasternodeSync::blockDepthUpToWhichToRequestMNWinners;
    if (winner.GetHeight() < nFirstBlock || winner.GetHeight() > chainTipHeight + 20) {
        LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.GetHeight(), chainTipHeight);
        return false;
    }

    if (!winner.ComputeScoreHash()) {
        LogPrint("mnpayments", "mnw - could not compute score hash for height %d\n", winner.GetHeight());
        return false;
    }

    CMasternode mn;
    if (!masternodeManager_.GetMNCopy(winner.vinMasternode,mn))
    {
        std::string strError = strprintf("Unknown Masternode %s", winner.vinMasternode.prevout.hash.ToString());
        LogPrint("masternode","%s - %s\n",__func__, strError);
        masternodeSynchronization_.AskForMN(pfrom, winner.vinMasternode);
        return false;
    }
    if (!CheckMasternodeWinnerValidity(winner,mn))
    {
        return false;
    }

    if (!CanVote(winner.vinMasternode.prevout, winner.GetScoreHash())) {
        //  LogPrint("masternode","mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
        return false;
    }

    if (!CheckMasternodeWinnerSignature(winner,mn.pubKeyMasternode)) {
        LogPrintf("%s : - invalid signature\n", __func__);
        if (masternodeSynchronization_.IsSynced()) Misbehaving(pfrom->GetNodeState(), 20,"Invalid signature for mnwinner.");
        // it could just be a non-synced masternode
        masternodeSynchronization_.AskForMN(pfrom, winner.vinMasternode);
        return false;
    }
    return true;
}
void CMasternodePayments::ProcessMasternodeWinners(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "mnw") { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if(pfrom->nVersion < ActiveProtocol() || !CheckMasternodeWinnerCandidate(pfrom,winner))
        {
            return;
        }

        if (AddWinningMasternode(winner)) {
            winner.Relay();
            masternodeSynchronization_.RecordMasternodeWinnerUpdate(winner.GetHash());
        }
    }
}

bool CMasternodePayments::GetBlockPayee(const uint256& scoringBlockHash, CScript& payee) const
{
    auto* payees = paymentData_.getPayeesForScoreHash(scoringBlockHash);
    if (payees != nullptr)
        return payees->GetPayee(payee);

    return false;
}
bool CMasternodePayments::CheckMasternodeWinnerSignature(const CMasternodePaymentWinner& winner,const CPubKey& mnPubKey) const
{
    std::string errorMessage = "";
    if(!CObfuScationSigner::VerifySignature<CMasternodePaymentWinner>(winner,mnPubKey,errorMessage))
    {
        return error("%s - Got bad Masternode address signature %s (%s)\n",__func__, winner.vinMasternode.prevout.hash, errorMessage);
    }
    return true;
}
bool CMasternodePayments::CheckMasternodeWinnerValidity(const CMasternodePaymentWinner& winner, CMasternode& masternode) const
{
    std::string strError ="";
    if (masternode.protocolVersion < ActiveProtocol()) {
        strError = strprintf("Masternode protocol too old %d - req %d", masternode.protocolVersion, ActiveProtocol());
        LogPrint("masternode","%s - %s\n",__func__, strError);
        return false;
    }

    /* Before accepting a payment as valid, explicitly check that the
       masternode is active.  GetMasternodeRank includes this check, but
       has a cache on results so double-checking doesn't hurt.  */
    masternodeManager_.Check(masternode);
    if (!masternode.IsEnabled()) {
        strError = strprintf("Masternode %s is not active", winner.vinMasternode.prevout.hash.ToString());
        LogPrint("masternode", "%s - %s\n",__func__, strError);
        return false;
    }

    const uint256& scoringBlockHash = winner.getScoringBlockHash();
    assert(!scoringBlockHash.IsNull());
    const unsigned voterRank = GetMasternodeRank(winner.vinMasternode, scoringBlockHash, ActiveProtocol(), 2 * MNPAYMENTS_SIGNATURES_TOTAL);

    if (voterRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (voterRank > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%u)", MNPAYMENTS_SIGNATURES_TOTAL * 2, voterRank);
            LogPrint("masternode","%s - %s\n",__func__, strError);
        }
        return false;
    }


    if(!masternodeSynchronization_.IsSynced()){ return true;}

    /* Make sure that the payee is in our own payment queue near the top.  */
    const MnPaymentQueueData mnQueueData = GetMasternodePaymentQueue(scoringBlockHash, winner.GetHeight());
    for(const CScript& mnPayee: mnQueueData.topTwentyMNPayees)
    {
        if(mnPayee == winner.payee) return true;
    }
    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(const CScript mnpayee, int nNotBlockHeight) const
{
    LOCK(cs_mapMasternodeBlocks);

    const CBlockIndex* tip = activeChain_.Tip();
    if (tip == nullptr)
        return false;

    for (int64_t h = 0; h <= 8; ++h) {
        if (tip->nHeight + h == nNotBlockHeight) continue;
        uint256 scoringBlockHash;
        if (!GetBlockHashForScoring(scoringBlockHash, tip, h)) continue;
        auto* payees = paymentData_.getPayeesForScoreHash(scoringBlockHash);
        CScript payee;
        if (payees != nullptr && payees->GetPayee(payee) && payee == mnpayee)
            return true;
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(const CMasternodePaymentWinner& winnerIn)
{
    return paymentData_.recordWinner(winnerIn);
}

bool CMasternodePayments::IsTransactionValid(const CMasternodeBlockPayees& payees, const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew) const
{
    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";
    auto rewards = subsidies.GetBlockSubsidity(payees.GetHeight());
    CAmount requiredMasternodePayment = rewards.nMasternodeReward;

    //require at least 6 signatures
    std::vector<CMasternodePayee> vecPayments = payees.GetPaymentVotes();
    for(const auto& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (const auto& payee : vecPayments) {
        bool found = false;
        for (const auto& out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredMasternodePayment)
                    found = true;
                else
                    LogPrint("masternode","Masternode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue), FormatMoney(requiredMasternodePayment));
            }
        }

        if (payee.nVotes >= CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            std::string addressString = ExtractDestination(payee.scriptPubKey);
            if (strPayeesPossible == "") {
                strPayeesPossible += addressString;
            } else {
                strPayeesPossible += "," + addressString;
            }
        }
    }

    LogPrint("masternode","CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment), strPayeesPossible);
    return false;
}

bool CMasternodePayments::IsTransactionValid(const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew, const uint256& scoringBlockHash) const
{
    LOCK(cs_mapMasternodeBlocks);

    auto* payees = paymentData_.getPayeesForScoreHash(scoringBlockHash);
    if (payees != nullptr)
        return IsTransactionValid(*payees,subsidies,txNew);

    return true;
}

void CMasternodePayments::PruneOldMasternodeWinnerData()
{
    const int nHeight = activeChain_.Height();
    if(nHeight >0) paymentData_.pruneOutdatedMasternodeWinners(nHeight);
}

CScript CMasternodePayments::GetNextMasternodePayeeInQueueForPayment(const CBlockIndex* pindex, const int offset) const
{
    const MnPaymentQueueData queueData = GetMasternodePaymentQueue(pindex, offset);
    return queueData.topTwentyMNPayees.empty()? CScript():queueData.topTwentyMNPayees.front();
}
MnPaymentQueueData CMasternodePayments::GetMasternodePaymentQueue(const CBlockIndex* pindex, int offset) const
{
    uint256 scoringBlockHash;
    if (!GetBlockHashForScoring(scoringBlockHash, pindex, offset))
        return {};

    const int64_t nBlockHeight = pindex->nHeight + offset;

    return GetMasternodePaymentQueue(scoringBlockHash, nBlockHeight);
}

void ComputeMasternodesAndScores(
    const CMasternodePayments& masternodePayments,
    std::vector<CMasternode>& masternodes,
    const uint256& scoringBlockHash,
    const int nMnCount,
    const int nBlockHeight,
    const bool fFilterSigTime,
    std::vector<CMasternode*>& masternodeQueue,
    std::map<const CMasternode*, uint256>& masternodeScores)
{
    BOOST_FOREACH (CMasternode& mn, masternodes)
    {
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < ActiveProtocol()) continue;

        // It's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        // On regtest, we ignore this criterion, because it makes it hard to do
        // proper testing with a very small number of masternodes (which would
        // be scheduled and skipped all the time).
        if (Params().NetworkID() != CBaseChainParams::REGTEST) {
            if (masternodePayments.IsScheduled(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()), nBlockHeight)) continue;
        }

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime())
        {
            continue;
        }
        //make sure it has as many confirmations as there are masternodes
        if (ComputeMasternodeInputAge(mn) < nMnCount) continue;

        masternodeQueue.push_back(&mn);
        masternodeScores[&mn] = mn.CalculateScore(scoringBlockHash);
    }
}

MnPaymentQueueData CMasternodePayments::GetMasternodePaymentQueue(const uint256& scoringBlockHash, const int nBlockHeight) const
{
    LOCK2(networkMessageManager_.cs_process_message,networkMessageManager_.cs);
    MnPaymentQueueData queueData;
    std::vector< CMasternode* > masternodeQueue;
    std::map<const CMasternode*, uint256> masternodeScores;
    std::vector<CMasternode> filteredMasternodes;

    masternodeManager_.Check();
    const int protocolVersion = ActiveProtocol();
    const int mnCount = std::count_if(
        networkMessageManager_.masternodes.begin(),
        networkMessageManager_.masternodes.end(),
        [protocolVersion](const CMasternode& mn)
        {
            return !(mn.protocolVersion < protocolVersion || !mn.IsEnabled());
        });
    ComputeMasternodesAndScores(
        *this,
        networkMessageManager_.masternodes,
        scoringBlockHash,
        mnCount,
        nBlockHeight,
        true,
        masternodeQueue,
        masternodeScores);
    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (static_cast<int>(masternodeQueue.size()) < mnCount / 3)
    {
        ComputeMasternodesAndScores(
            *this,
            networkMessageManager_.masternodes,
            scoringBlockHash,
            mnCount,
            nBlockHeight,
            false,
            masternodeQueue,
            masternodeScores);
    }


    std::sort(masternodeQueue.begin(), masternodeQueue.end(),
        [&masternodeScores](const CMasternode* a, const CMasternode* b)
        {
            if(!b) return true;
            if(!a) return false;

            uint256 aScore = masternodeScores[a];
            uint256 bScore = masternodeScores[b];
            return (aScore > bScore);
        }   );

    queueData.topTwentyMNPayees.reserve(2 * MNPAYMENTS_SIGNATURES_TOTAL);
    queueData.queueSize = masternodeQueue.size();
    for(CMasternode* pmn: masternodeQueue)
    {
        if(pmn != nullptr && queueData.topTwentyMNPayees.size() < 2 * MNPAYMENTS_SIGNATURES_TOTAL)
        {
            queueData.topTwentyMNPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
        }
        else
        {
            break;
        }
    }
    return queueData;
}


namespace
{

/** Checks if the given masternode is deemed "ok" based on the minimum
 *  masternode age for winners, the minimum protocol version and being active
 *  at all.  If so, returns true and sets its score.  */
bool CheckAndGetScore(CMasternode& mn,
                      const uint256& scoringBlockHash, const int minProtocol,
                      int64_t& score)
{
    if (mn.protocolVersion < minProtocol) {
        LogPrint("masternode", "Skipping Masternode with obsolete version %d\n", mn.protocolVersion);
        return false;
    }

    const int64_t nAge = GetAdjustedTime() - mn.sigTime;
    const int64_t minimumAge = Params().NetworkID() != CBaseChainParams::REGTEST? MN_WINNER_MINIMUM_AGE : 60 * 25;
    if (nAge < minimumAge)
    {
        LogPrint("masternode", "Skipping just activated Masternode. Age: %ld\n", nAge);
        return false;
    }

    if (!mn.IsEnabled ())
        return false;

    const uint256 n = mn.CalculateScore(scoringBlockHash);
    score = n.GetCompact(false);

    return true;
}

} // anonymous namespace

unsigned CMasternodePayments::GetMasternodeRank(const CTxIn& vin, const uint256& scoringBlockHash, int minProtocol, const unsigned nCheckNum) const
{
    assert(nCheckNum <= MAX_RANKING_CHECK_NUM);

    const RankingCacheEntry::value_type* cacheEntry;
    RankingCacheEntry::value_type newEntry;

    cacheEntry = rankingCache->Find(scoringBlockHash);
    if (cacheEntry == nullptr) {
        std::vector<std::pair<int64_t, uint256>> rankedNodes;
        {
            LOCK(networkMessageManager_.cs);
            masternodeManager_.Check();
            for (auto& mn : networkMessageManager_.masternodes) {
                int64_t score;
                if (!CheckAndGetScore(mn, scoringBlockHash, minProtocol, score))
                    continue;

                rankedNodes.emplace_back(score, mn.vin.prevout.hash);
            }
        }

        std::sort(rankedNodes.begin(), rankedNodes.end(),
            [] (const std::pair<int64_t, uint256>& a, const std::pair<int64_t, uint256>& b)
            {
                return a.first > b.first;
            });

        for (unsigned i = 0; i < newEntry.size(); ++i)
            if (i < rankedNodes.size())
                newEntry[i] = rankedNodes[i].second;
            else
                newEntry[i].SetNull();

        rankingCache->Insert(scoringBlockHash, newEntry);
        cacheEntry = &newEntry;
    }

    assert(cacheEntry != nullptr);
    for (unsigned i = 0; i < cacheEntry->size(); ++i)
        if ((*cacheEntry)[i] == vin.prevout.hash)
            return i + 1;

    return static_cast<unsigned>(-1);
}

void CMasternodePayments::ResetRankingCache()
{
    rankingCache.reset(new RankingCache);
}
