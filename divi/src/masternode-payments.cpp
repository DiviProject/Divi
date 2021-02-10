// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"

#include "addrman.h"
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

void Misbehaving(NodeId pnode, int howmuch);
extern CCriticalSection cs_main;
extern CChain chainActive;
extern CMasternodeMan mnodeman;

const int CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED = 6;
const int CMasternodePayments::MNPAYMENTS_SIGNATURES_TOTAL = 10;

/** Object for who's going to get paid on which blocks */

CMasternodePayments::CMasternodePayments(
    MasternodePaymentData& paymentData,
    CMasternodeMan& masternodeManager
    ): nSyncedFromPeer(0)
    , nLastBlockHeight(0)
    , chainTipHeight(0)
    , paymentData_(paymentData)
    , masternodeManager_(masternodeManager)
    , mapMasternodePayeeVotes(paymentData_.mapMasternodePayeeVotes)
    , mapMasternodeBlocks(paymentData_.mapMasternodeBlocks)
{
}

void CMasternodePayments::Clear()
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
    mapMasternodeBlocks.clear();
    mapMasternodePayeeVotes.clear();
}

bool CMasternodePayments::CanVote(const COutPoint& outMasternode, const uint256& seedHash)
{
    LOCK(cs_mapMasternodePayeeVotes);

    const auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees == nullptr)
        return true;

    return payees->CanVote(outMasternode);
}


void CMasternodePayments::FillBlockPayee(const CBlockIndex* pindexPrev, CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake) const
{
    bool hasPayment = true;
    CScript payee;

    //spork
    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindexPrev, 1)) {
        LogPrint("masternode", "FillBlockPayee - failed to get score hash\n");
        return;
    }
    if (!GetBlockPayee(seedHash, payee)) {
        // No masternode detected, fall back to our own queue.
        payee = GetNextMasternodePayeeInQueueForPayment(pindexPrev, 1, true);
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

        LogPrint("masternode","Masternode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), address2.ToString().c_str());
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto() const
{
    //    if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
    return ActiveProtocol();                          // Allow only updated peers
}

void CMasternodePayments::ProcessMessageMasternodePayments(CMasternodeSync& masternodeSynchronization,CNode* pfrom, const std::string& strCommand, CDataStream& vRecv)
{
    if (!IsBlockchainSynced()) return;

    if (strCommand == "mnget") { //Masternode Payments Request Sync

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, "mnget"))
        {
            LogPrintf("%s : mnget - peer already asked me for the list\n", __func__);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        netfulfilledman.AddFulfilledRequest(pfrom->addr, "mnget");
        Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (GetPaymentWinnerForHash(winner.GetHash()) != nullptr) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSynchronization.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (masternodeManager_.CountEnabled() * 1.25);
        if (winner.GetHeight() < nFirstBlock || winner.GetHeight() > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.GetHeight(), nHeight);
            return;
        }

        if (!winner.ComputeScoreHash()) {
            LogPrint("mnpayments", "mnw - could not compute score hash for height %d\n", winner.GetHeight());
            return;
        }

        std::string strError = "";
        if (!CheckMasternodeWinnerValidity(masternodeSynchronization,winner,pfrom,strError))
        {
            return;
        }

        if (!CanVote(winner.vinMasternode.prevout, winner.GetScoreHash())) {
            //  LogPrint("masternode","mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!CheckMasternodeWinnerSignature(winner)) {
            LogPrintf("%s : - invalid signature\n", __func__);
            if (masternodeSynchronization.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced masternode
            masternodeManager_.AskForMN(pfrom, winner.vinMasternode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (AddWinningMasternode(winner)) {
            winner.Relay();
            masternodeSynchronization.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePayments::GetBlockPayee(const uint256& seedHash, CScript& payee) const
{
    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->GetPayee(payee);

    return false;
}
bool CMasternodePayments::CheckMasternodeWinnerSignature(const CMasternodePaymentWinner& winner) const
{
    CMasternode* pmn = masternodeManager_.Find(winner.vinMasternode);

    if (pmn != NULL) {
        std::string errorMessage = "";
        if(!CObfuScationSigner::VerifySignature<CMasternodePaymentWinner>(winner,pmn->pubKeyMasternode,errorMessage))
        {
            return error("%s - Got bad Masternode address signature %s (%s)\n",__func__, winner.vinMasternode.prevout.hash.ToString(),errorMessage);
        }
        return true;
    }

    return false;
}
bool CMasternodePayments::CheckMasternodeWinnerValidity(CMasternodeSync& masternodeSynchronization, const CMasternodePaymentWinner& winner, CNode* pnode, std::string& strError) const
{
    CMasternode* pmn = masternodeManager_.Find(winner.vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", winner.vinMasternode.prevout.hash.ToString());
        LogPrint("masternode","%s - %s\n",__func__, strError);
        masternodeManager_.AskForMN(pnode, winner.vinMasternode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("masternode","%s - %s\n",__func__, strError);
        return false;
    }

    /* Before accepting a payment as valid, explicitly check that the
       masternode is active.  GetMasternodeRank includes this check, but
       has a cache on results so double-checking doesn't hurt.  */
    pmn->Check();
    if (!pmn->IsEnabled()) {
        strError = strprintf("Masternode %s is not active", winner.vinMasternode.prevout.hash.ToString());
        LogPrint("masternode", "%s - %s\n",__func__, strError);
        return false;
    }

    const uint256& seedHash = winner.getSeedHash();
    assert(!seedHash.IsNull());
    const unsigned voterRank = masternodeManager_.GetMasternodeRank(winner.vinMasternode, seedHash, ActiveProtocol(), 2 * MNPAYMENTS_SIGNATURES_TOTAL);

    if (voterRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (voterRank > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%u)", MNPAYMENTS_SIGNATURES_TOTAL * 2, voterRank);
            LogPrint("masternode","%s - %s\n",__func__, strError);
        }
        return false;
    }


    if(!masternodeSynchronization.IsSynced()){ return true;}

    /* Make sure that the payee is in our own payment queue near the top.  */
    const std::vector<CMasternode*> mnQueue = GetMasternodePaymentQueue(seedHash, winner.GetHeight(), true);
    for (int i = 0; i < std::min<int>(2 * MNPAYMENTS_SIGNATURES_TOTAL, mnQueue.size()); ++i) {
        const auto& mn = *mnQueue[i];
        const CScript mnPayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (mnPayee == winner.payee)
            return true;
    }
    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(const CScript mnpayee, int nNotBlockHeight) const
{
    LOCK(cs_mapMasternodeBlocks);

    CBlockIndex* tip = nullptr;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked) return false;
        tip = chainActive.Tip();
    }
    if (tip == nullptr)
        return false;

    for (int64_t h = 0; h <= 8; ++h) {
        if (tip->nHeight + h == nNotBlockHeight) continue;
        uint256 seedHash;
        if (!GetBlockHashForScoring(seedHash, tip, h)) continue;
        auto* payees = GetPayeesForScoreHash(seedHash);
        CScript payee;
        if (payees != nullptr && payees->GetPayee(payee) && payee == mnpayee)
            return true;
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(const CMasternodePaymentWinner& winnerIn)
{
    CMasternodeBlockPayees* payees;
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

        if (GetPaymentWinnerForHash(winnerIn.GetHash()) != nullptr)
            return false;

        auto ins = mapMasternodePayeeVotes.emplace(winnerIn.GetHash(), winnerIn);
        assert(ins.second);

        payees = GetPayeesForScoreHash(winnerIn.GetScoreHash());
        if (payees == nullptr) {
            CMasternodeBlockPayees blockPayees(winnerIn.GetHeight());
            auto mit = mapMasternodeBlocks.emplace(winnerIn.GetScoreHash(), std::move(blockPayees)).first;
            payees = &mit->second;
        }
    }

    payees->CountVote(winnerIn.vinMasternode.prevout, winnerIn.payee);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const I_BlockSubsidyProvider& subsidies, const CTransaction& txNew) const
{
    LOCK(cs_vecPayments);
    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";
    auto rewards = subsidies.GetBlockSubsidity(nBlockHeight);

    CAmount requiredMasternodePayment = rewards.nMasternodeReward;

    //require at least 6 signatures
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
                    LogPrint("masternode","Masternode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
        }

        if (payee.nVotes >= CMasternodePayments::MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("masternode","CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString() const
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (const auto& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(const uint256& seedHash) const
{
    LOCK(cs_mapMasternodeBlocks);

    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->GetRequiredPaymentsString();

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew, const uint256& seedHash) const
{
    LOCK(cs_mapMasternodeBlocks);

    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->IsTransactionValid(subsidies,txNew);

    return true;
}

void CMasternodePayments::CheckAndRemove()
{
}

void CMasternodePayments::PruneOldMasternodeWinnerData(CMasternodeSync& masternodeSynchronization)
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(masternodeSynchronization.masternodeCount() * 1.25), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.GetHeight() > nLimit) {
            LogPrint("mnpayments", "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.GetHeight());
            masternodeSynchronization.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.GetScoreHash());
        } else {
            ++it;
        }
    }
}

void CMasternodePayments::updateChainTipHeight(const CBlockIndex* chainTip)
{
    chainTipHeight = chainTip->nHeight;
}
void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapMasternodePayeeVotes);

    int nCount = (masternodeManager_.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.GetHeight() >= chainTipHeight - nCountNeeded && winner.GetHeight() <= chainTipHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", MASTERNODE_SYNC_MNW, nInvCount);
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

unsigned CMasternodePayments::FindLastPayeePaymentTime(const CMasternode& masternode, const unsigned maxBlockDepth) const
{
    const CBlockIndex* chainTip = chainActive.Tip();
    if (chainTip == NULL) return 0u;

    CScript mnPayee = GetScriptForDestination(masternode.pubKeyCollateralAddress.GetID());
    unsigned n = 0;
    for (unsigned int i = 1; chainTip && chainTip->nHeight > 0; i++) {
        if (n >= maxBlockDepth) {
            return 0u;
        }
        n++;

        uint256 seedHash;
        if (!GetBlockHashForScoring(seedHash, chainTip, 0))
            continue;

        auto* masternodePayees = GetPayeesForScoreHash(seedHash);
        if (masternodePayees != nullptr) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (masternodePayees->HasPayeeWithVotes(mnPayee, 2)) {
                return chainTip->nTime + masternode.DeterministicTimeOffset();
            }
        }

        if (chainTip->pprev == NULL) {
            assert(chainTip);
            break;
        }
        chainTip = chainTip->pprev;
    }
    return 0u;
}

CScript CMasternodePayments::GetNextMasternodePayeeInQueueForPayment(const CBlockIndex* pindex, const int offset, bool fFilterSigTime) const
{
    std::vector<CMasternode*> mnQueue = GetMasternodePaymentQueue(pindex, offset, fFilterSigTime);
    const CMasternode* mn = (!mnQueue.empty())? mnQueue.front(): NULL;
    return mn? mn->GetPaymentScript(): CScript();
}
std::vector<CMasternode*> CMasternodePayments::GetMasternodePaymentQueue(const CBlockIndex* pindex, int offset, bool fFilterSigTime) const
{
    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindex, offset))
        return {};

    const int64_t nBlockHeight = pindex->nHeight + offset;

    return GetMasternodePaymentQueue(seedHash, nBlockHeight, fFilterSigTime);
}

std::vector<CMasternode*> CMasternodePayments::GetMasternodePaymentQueue(const uint256& seedHash, const int nBlockHeight, bool fFilterSigTime) const
{
    LockableMasternodeData mnData = masternodeManager_.GetLockableMasternodeData();
    LOCK(mnData.cs);
    std::vector< CMasternode* > masternodeQueue;
    std::map<const CMasternode*, uint256> masternodeScores;

    int nMnCount = masternodeManager_.CountEnabled();
    BOOST_FOREACH (CMasternode& mn, mnData.masternodes)
    {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < ActiveProtocol()) continue;

        // It's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        // On regtest, we ignore this criterion, because it makes it hard to do
        // proper testing with a very small number of masternodes (which would
        // be scheduled and skipped all the time).
        if (Params().NetworkID() != CBaseChainParams::REGTEST) {
            if (IsScheduled(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()), nBlockHeight)) continue;
        }

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are masternodes
        if (mn.GetMasternodeInputAge() < nMnCount) continue;

        masternodeQueue.push_back(&mn);
        masternodeScores[&mn] = mn.CalculateScore(seedHash);
    }

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && static_cast<int>(masternodeQueue.size()) < nMnCount / 3) return GetMasternodePaymentQueue(seedHash, nBlockHeight, false);


    std::sort(masternodeQueue.begin(), masternodeQueue.end(),
        [&masternodeScores](const CMasternode* a, const CMasternode* b)
        {
            if(!b) return true;
            if(!a) return false;

            uint256 aScore = masternodeScores[a];
            uint256 bScore = masternodeScores[b];
            return (aScore > bScore);
        }   );
    return masternodeQueue;
}