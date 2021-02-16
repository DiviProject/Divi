// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"

#include <activemasternode.h>
#include <addrman.h>
#include <masternode.h>
#include <mruset.h>
#include <obfuscation.h>
#include <spork.h>
#include <Logging.h>
#include <masternode-sync.h>
#include <MasternodeNetworkMessageManager.h>
#include <boost/lexical_cast.hpp>
#include <utiltime.h>
#include <version.h>
#include <MasternodeHelpers.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chain.h>
#include <blockmap.h>
#include <BlockDiskAccessor.h>

#include <array>

extern void Misbehaving(NodeId pnode, int howmuch);

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Maximum nCheckNum that we allow for ranking masternodes.
 *  In practice, we only need to check if a node is in the top 10
 *  (or rather, top-20 to give some leeway before punishing nodes
 *  with wrong claims).  */
static constexpr unsigned MAX_RANKING_CHECK_NUM = 20;

/** Number of entries (blocks) we keep in the cache of ranked masternodes.  */
static constexpr unsigned RANKING_CACHE_SIZE = 2500;

//    pathMN = GetDataDir() / "mncache.dat";
//    strMagicMessage = "MasternodeCache";

namespace
{
static bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, MasternodeTier nMasternodeTier)
{
    CScript payee2;
    payee2 = GetScriptForDestination(pubkey.GetID());

    CTransaction txVin;
    uint256 hash;
    auto nCollateral = CMasternode::GetTierCollateralAmount(nMasternodeTier);
    if (GetTransaction(vin.prevout.hash, txVin, hash, true)) {
        BOOST_FOREACH (CTxOut out, txVin.vout) {
            if (out.nValue == nCollateral) {
                if (out.scriptPubKey == payee2) return true;
            }
        }
    }

    return false;
}

/**
 * An entry in the ranking cache.  We use mruset to hold the cache,
 * which means that even though it is conceptually a map, we represent
 * it as a set, i.e. instances of this class are both the key and value
 * in one, and compare based on the key.
 */
struct RankingCacheEntry
{

  using value_type = std::array<uint256, MAX_RANKING_CHECK_NUM>;

  /** The scoring hash this is for, i.e. the key.  */
  uint256 seedHash;

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
  return a.seedHash == b.seedHash;
}

bool operator<(const RankingCacheEntry& a, const RankingCacheEntry& b)
{
  return a.seedHash < b.seedHash;
}

} // anonymous namespace

/**
 * Internal helper class that represents the cache of the best MAX_RANKING_NUM
 * nodes for recent block heights.
 */
class CMasternodeMan::RankingCache
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

  /** Looks up an entry by seed hash and returns it, or a null
   *  pointer if there is no matching entry.  */
  const RankingCacheEntry::value_type* Find(const uint256& hash) const
  {
    RankingCacheEntry entry;
    entry.seedHash = hash;

    auto mit = entries.find(entry);
    if (mit == entries.end())
      return nullptr;

    return &mit->bestVins;
  }

  /** Inserts an entry into the cache.  */
  void Insert(const uint256& hash, const RankingCacheEntry::value_type& bestVins)
  {
    RankingCacheEntry entry;
    entry.seedHash = hash;
    entry.bestVins = bestVins;

    auto ins = entries.insert(std::move(entry));
    assert(ins.second);
  }

};

CMasternodeMan::~CMasternodeMan()
{
    rankingCache.reset();
}

CMasternodeMan::CMasternodeMan(
    MasternodeNetworkMessageManager& networkMessageManager,
    const CChain& activeChain,
    const BlockMap& blockIndicesByHash,
    CAddrMan& addressManager
    ):  networkMessageManager_(networkMessageManager)
    , cs(networkMessageManager_.cs)
    , cs_process_message()
    , rankingCache(new RankingCache)
    , activeChain_(activeChain)
    , blockIndicesByHash_(blockIndicesByHash)
    , addressManager_(addressManager)
{
}

bool CMasternodeMan::Add(const CMasternode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CMasternode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("masternode", "CMasternodeMan: Adding new Masternode %s - %i now\n", mn.vin.prevout.hash.ToString(), networkMessageManager_.masternodeCount() + 1);
        networkMessageManager_.masternodes.push_back(mn);
        return true;
    }

    return false;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, networkMessageManager_.masternodes) {
        mn.Check();
    }
}

std::vector<CMasternode> CMasternodeMan::GetFullMasternodeVector() const
{
    LOCK(cs);
    return networkMessageManager_.masternodes;
}

void CMasternodeMan::CheckAndRemoveInnactive(CMasternodeSync& masternodeSynchronization, bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    std::vector<CMasternode>::iterator it = networkMessageManager_.masternodes.begin();
    while (it != networkMessageManager_.masternodes.end())
    {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE ||
            (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < ActiveProtocol())
        {
            LogPrint("masternode", "CMasternodeMan: Removing inactive Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), networkMessageManager_.masternodeCount() - 1);

            networkMessageManager_.clearExpiredMasternodeBroadcasts(it->vin.prevout,masternodeSynchronization);
            networkMessageManager_.clearExpiredMasternodeEntryRequests(it->vin.prevout);
            it = networkMessageManager_.masternodes.erase(it);
        } else {
            ++it;
        }
    }

    networkMessageManager_.clearTimedOutMasternodeListRequestsFromPeers();
    networkMessageManager_.clearTimedOutMasternodeListRequestsToPeers();
    networkMessageManager_.clearTimedOutMasternodeEntryRequests();
    networkMessageManager_.clearTimedOutMasternodeBroadcasts(masternodeSynchronization);
    networkMessageManager_.clearTimedOutMasternodePings();
}

bool CMasternodeMan::UpdateWithNewBroadcast(const CMasternodeBroadcast &mnb, CMasternode& masternode) const
{
    if (mnb.sigTime > masternode.sigTime) {
        masternode.pubKeyMasternode = mnb.pubKeyMasternode;
        masternode.pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        masternode.sigTime = mnb.sigTime;
        masternode.signature = mnb.signature;
        masternode.protocolVersion = mnb.protocolVersion;
        masternode.addr = mnb.addr;
        masternode.lastTimeChecked = 0;
        return true;
    }
    return false;
}
bool CMasternodeMan::CheckInputsForMasternode(const CMasternodeBroadcast& mnb, int& nDoS)
{
    // search existing Masternode list
    // nothing to do here if we already know about this masternode and it's enabled
    const CMasternode* pmn = Find(mnb.vin);
    if (pmn != nullptr && pmn->IsEnabled())
        return true;

    if (CMasternode::IsCoinSpent(mnb.vin.prevout, mnb.nTier))
    {
        LogPrint("masternode", "mnb - coin is already spent\n");
        return false;
    }


    LogPrint("masternode", "mnb - Accepted Masternode entry\n");

    const CBlockIndex* pindexConf = ComputeMasternodeConfirmationBlockIndex(mnb);

    if (pindexConf == nullptr) {
        LogPrint("masternode","mnb - Input must have at least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 PIV tx got MASTERNODE_MIN_CONFIRMATIONS
    if (pindexConf->GetBlockTime() > mnb.sigTime) {
        LogPrint("masternode","mnb - Bad sigTime %d for Masternode %s (%i conf block is at %d)\n",
                 mnb.sigTime, mnb.vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pindexConf->GetBlockTime());
        return false;
    }

    return true;
}
bool CMasternodeMan::CheckAndUpdateMasternode(CMasternodeSync& masternodeSynchronization, CMasternodeBroadcast& mnb, int& nDoS)
{
    // make sure signature isn't in the future (past is OK)
    if (mnb.sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("%s : mnb - Signature rejected, too far into the future %s\n", __func__, mnb.vin.prevout.hash.ToString());
        nDoS = 1;
        return false;
    }

    if(!CMasternode::IsTierValid(static_cast<MasternodeTier>(mnb.nTier))) {
        LogPrintf("%s : mnb - Invalid tier: %d\n", __func__, static_cast<int>(mnb.nTier));
        nDoS = 20;
        return false;
    }

    if (mnb.protocolVersion < ActiveProtocol()) {
        LogPrint("masternode","mnb - ignoring outdated Masternode %s protocol version %d\n", mnb.vin.prevout.hash.ToString(), mnb.protocolVersion);
        return false;
    }

    if (!mnb.vin.scriptSig.empty()) {
        LogPrint("masternode","mnb - Ignore Not Empty ScriptSig %s\n", mnb.vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if(!CObfuScationSigner::VerifySignature<CMasternodeBroadcast>(mnb,mnb.pubKeyCollateralAddress,errorMessage))
    {
        LogPrintf("%s : - Got bad Masternode address signature (%s)\n", __func__, errorMessage);
        nDoS = 100;
        return false;
    }

    //search existing Masternode list, this is where we update existing Masternodes with new mnb broadcasts
    CMasternode* pmn = Find(mnb.vin);

    // no such masternode, nothing to update
    if (pmn == NULL)
        return true;

    // this broadcast older than we have, it's bad.
    if (pmn->sigTime > mnb.sigTime) {
        LogPrint("masternode","mnb - Bad sigTime %d for Masternode %s (existing broadcast is at %d)\n",
                 mnb.sigTime, mnb.vin.prevout.hash.ToString(), pmn->sigTime);
        return false;
    }
    // masternode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == mnb.pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(MASTERNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("masternode","mnb - Got updated entry for %s\n", mnb.vin.prevout.hash.ToString());
        if (UpdateWithNewBroadcast(mnb,*pmn)) {
            int unusedDoSValue = 0;
            if (mnb.lastPing != CMasternodePing() &&  CheckAndUpdatePing(*pmn,mnb.lastPing,unusedDoSValue,false)) {
                RecordSeenPing(pmn->lastPing);
                pmn->lastPing.Relay();
            }
            pmn->Check();
            if (pmn->IsEnabled()) mnb.Relay();
        }
        masternodeSynchronization.AddedMasternodeList(mnb.GetHash());
    }

    return true;
}

bool CMasternodeMan::CheckAndUpdatePing(CMasternode& mn, CMasternodePing& mnp, int& nDoS,bool fRequireEnabled)
{
    if (mnp.sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("masternode", "%s - Signature rejected, too far into the future %s\n",
                 __func__, mnp.vin.prevout.hash.ToString());
        nDoS = 1;
        return false;
    }

    if (mnp.sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("masternode", "%s - Signature rejected, too far into the past %s - %d %d\n",
                 __func__, mnp.vin.prevout.hash.ToString(), mnp.sigTime, GetAdjustedTime());
        nDoS = 1;
        return false;
    }

    LogPrint("masternode", "%s - New Ping - %s - %lli\n", __func__, mnp.blockHash.ToString(), mnp.sigTime);

    // see if we have this Masternode
    if (mn.protocolVersion >= ActiveProtocol()) {
        if (fRequireEnabled && !mn.IsEnabled()) return false;

        // LogPrint("masternode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this masternode or
        // last ping was more then MASTERNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!mn.IsTooEarlyToReceivePingUpdate(mnp.sigTime)) {
            std::string errorMessage = "";
            if (!CObfuScationSigner::VerifySignature<CMasternodePing>(mnp,mn.pubKeyMasternode,errorMessage))
            {
                LogPrint("masternode", "%s - Got bad Masternode address signature %s (%s)\n",
                         __func__, mnp.vin.prevout.hash.ToString(),errorMessage);
                nDoS = 33;
                return false;
            }

            BlockMap::const_iterator mi = blockIndicesByHash_.find(mnp.blockHash);
            if (mi != blockIndicesByHash_.end() && (*mi).second) {
                if ((*mi).second->nHeight < activeChain_.Height() - 24) {
                    LogPrint("masternode", "%s - Masternode %s block hash %s is too old\n",
                             __func__, mnp.vin.prevout.hash.ToString(), mnp.blockHash.ToString());
                    // Do nothing here (no Masternode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                LogPrint("masternode", "%s - Masternode %s block hash %s is unknown\n",
                         __func__, mnp.vin.prevout.hash.ToString(), mnp.blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            mn.lastPing = mnp;

            mn.Check(true);
            if (!mn.IsEnabled()) return false;

            LogPrint("masternode", "%s - Masternode ping accepted, vin: %s\n",
                     __func__, mnp.vin.prevout.hash.ToString());
            return true;
        }
        LogPrint("masternode", "%s - Masternode ping arrived too early, vin: %s\n",
                 __func__, mnp.vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("masternode", "%s - Couldn't find compatible Masternode entry, vin: %s\n",
             __func__, mnp.vin.prevout.hash.ToString());

    return false;
}


void CMasternodeMan::Clear()
{
    networkMessageManager_.Clear();
}

int CMasternodeMan::stable_size ()
{
    LOCK(cs);
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    for (auto& mn : networkMessageManager_.masternodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (sporkManager.IsSporkActive (SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMasternode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMasternode_Age) < nMasternode_Min_Age) {
                continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled masternodes

        nStable_size++;
    }

    return nStable_size;
}

int CMasternodeMan::CountEnabled(int protocolVersion) const
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? ActiveProtocol() : protocolVersion;

    for (const auto& mn : networkMessageManager_.masternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMasternodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? ActiveProtocol() : protocolVersion;

    BOOST_FOREACH (CMasternode& mn, networkMessageManager_.masternodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

CMasternode* CMasternodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, networkMessageManager_.masternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CMasternode* CMasternodeMan::Find(const CPubKey& pubKeyMasternode)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, networkMessageManager_.masternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
LockableMasternodeData CMasternodeMan::GetLockableMasternodeData()
{
    return {cs,networkMessageManager_.masternodes};
}

namespace
{

/** Checks if the given masternode is deemed "ok" based on the minimum
 *  masternode age for winners, the minimum protocol version and being active
 *  at all.  If so, returns true and sets its score.  */
bool CheckAndGetScore(CMasternode& mn,
                      const uint256& seedHash, const int minProtocol,
                      int64_t& score)
{
    if (mn.protocolVersion < minProtocol) {
        LogPrint("masternode", "Skipping Masternode with obsolete version %d\n", mn.protocolVersion);
        return false;
    }

    const int64_t nAge = GetAdjustedTime() - mn.sigTime;
    if (nAge < MN_WINNER_MINIMUM_AGE) {
        LogPrint("masternode", "Skipping just activated Masternode. Age: %ld\n", nAge);
        return false;
    }

    mn.Check ();
    if (!mn.IsEnabled ())
        return false;

    const uint256 n = mn.CalculateScore(seedHash);
    score = n.GetCompact(false);

    return true;
}

} // anonymous namespace

unsigned CMasternodeMan::GetMasternodeRank(const CTxIn& vin, const uint256& seedHash, int minProtocol, const unsigned nCheckNum)
{
    assert(nCheckNum <= MAX_RANKING_CHECK_NUM);

    const RankingCacheEntry::value_type* cacheEntry;
    RankingCacheEntry::value_type newEntry;

    cacheEntry = rankingCache->Find(seedHash);
    if (cacheEntry == nullptr) {
        std::vector<std::pair<int64_t, uint256>> rankedNodes;
        {
            LOCK(cs);
            for (auto& mn : networkMessageManager_.masternodes) {
                int64_t score;
                if (!CheckAndGetScore(mn, seedHash, minProtocol, score))
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

        rankingCache->Insert(seedHash, newEntry);
        cacheEntry = &newEntry;
    }

    assert(cacheEntry != nullptr);
    for (unsigned i = 0; i < cacheEntry->size(); ++i)
        if ((*cacheEntry)[i] == vin.prevout.hash)
            return i + 1;

    return static_cast<unsigned>(-1);
}

void CMasternodeMan::ProcessMasternodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST)
        return;
}

void CMasternodeMan::RecordSeenPing(const CMasternodePing& mnp)
{
    networkMessageManager_.mapSeenMasternodePing[mnp.GetHash()] = mnp;

    const auto* pmn = Find(mnp.vin);
    if (pmn != nullptr) {
        const uint256 mnbHash = CMasternodeBroadcast(*pmn).GetHash();
        auto mit = networkMessageManager_.mapSeenMasternodeBroadcast.find(mnbHash);
        if (mit != networkMessageManager_.mapSeenMasternodeBroadcast.end())
          mit->second.lastPing = mnp;
    }
}

bool CMasternodeMan::ProcessBroadcast(CActiveMasternode& localMasternode, CMasternodeSync& masternodeSynchronization,CNode* pfrom, CMasternodeBroadcast& mnb)
{
    if (networkMessageManager_.mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
        masternodeSynchronization.AddedMasternodeList(mnb.GetHash());
        return true;
    }

    int nDoS = 0;
    if (!CheckAndUpdateMasternode(masternodeSynchronization,mnb,nDoS))
    {
        if (nDoS > 0 && pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Masternode
    //  - this is expensive, so it's only done once per Masternode
    if (!IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress, static_cast<MasternodeTier>(mnb.nTier))) {
        LogPrintf("%s : mnb - Got mismatched pubkey and vin\n", __func__);
        if (pfrom != nullptr)
            Misbehaving(pfrom->GetId(), 33);
        return false;
    }

    // make sure collateral is still unspent
    if (!localMasternode.IsOurBroadcast(mnb) && !CheckInputsForMasternode(mnb,nDoS))
    {
        LogPrintf("%s : - Rejected Masternode entry %s\n", __func__, mnb.vin.prevout.hash.ToString());
        if (nDoS > 0 && pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
        return false;
    }

    // Also check that the attached ping is valid.
    CMasternode mn(mnb);
    mn.lastPing = CMasternodePing();
    if (!CheckAndUpdatePing(mn,mnb.lastPing, nDoS)) {
        LogPrintf("%s : mnb - attached ping is invalid\n", __func__);
        if (pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
        return false;
    }

    // use this as a peer
    CNetAddr addr("127.0.0.1");
    if (pfrom != nullptr)
        addr = pfrom->addr;
    addressManager_.Add(CAddress(mnb.addr), addr, 2 * 60 * 60);
    masternodeSynchronization.AddedMasternodeList(mnb.GetHash());

    // If the masternode already is in our list and is enabled, nothing
    // remains to be done.  If it is not enabled, we remove the old masternode
    // first before adding it back in.
    const CMasternode* pmn = Find(mnb.vin);
    if (pmn != nullptr) {
        if (pmn->IsEnabled())
            return true;
        Remove(mnb.vin);
    }

    LogPrint("masternode","mnb - Got NEW Masternode entry - %s - %lli \n", mnb.vin.prevout.hash.ToString(), mnb.sigTime);
    Add(mn);

    networkMessageManager_.mapSeenMasternodeBroadcast[mnb.GetHash()] = mnb;
    RecordSeenPing(mnb.lastPing);

    // if it matches our Masternode privkey, then we've been remotely activated
    if (mnb.pubKeyMasternode == localMasternode.pubKeyMasternode && mnb.protocolVersion == PROTOCOL_VERSION) {
        localMasternode.EnableHotColdMasterNode(mnb.vin, mnb.addr);
    }

    const bool isLocal = mnb.addr.IsRFC1918() || mnb.addr.IsLocal();
    if (!isLocal || Params().NetworkID() == CBaseChainParams::REGTEST)
        mnb.Relay();

    return true;
}

bool CMasternodeMan::ProcessPing(CNode* pfrom, CMasternodePing& mnp, CMasternodeSync& masternodeSynchronization)
{
    if (networkMessageManager_.mapSeenMasternodePing.count(mnp.GetHash())) return true; //seen

    auto* pmn = Find(mnp.vin);
    int nDoS = 0;
    if (pmn != nullptr && CheckAndUpdatePing(*pmn,mnp,nDoS)) {
        RecordSeenPing(mnp);
        mnp.Relay();
        return true;
    }

    if (nDoS > 0) {
        // if anything significant failed, mark that node
        if (pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
    } else {
        // if the masternode is known, don't ask for the mnb, just return
        if (pmn != nullptr) return false;
    }

    // something significant is broken or mn is unknown,
    // we might have to ask for a masternode entry once
    if (pfrom != nullptr)
        masternodeSynchronization.AskForMN(pfrom, mnp.vin);

    return false;
}
bool CMasternodeMan::NotifyPeerOfMasternode(const CMasternode& mn, CNode* peer)
{
    if (!mn.addr.IsRFC1918() && mn.IsEnabled())
    {
        CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
        const uint256 hash = mnb.GetHash();
        peer->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
        if (!networkMessageManager_.mapSeenMasternodeBroadcast.count(hash)) networkMessageManager_.mapSeenMasternodeBroadcast.insert(std::make_pair(hash, mnb));
        return true;
    }
    return false;
}
void CMasternodeMan::SyncMasternodeListWithPeer(CNode* peer)
{
    LOCK(cs);
    int nInvCount = 0;
    for (const CMasternode& mn: networkMessageManager_.masternodes)
    {
        if (NotifyPeerOfMasternode(mn,peer))
        {
            LogPrint("masternode", "dseg - Sending Masternode entry - %s \n", mn.vin.prevout.hash.ToString());
            nInvCount++;
        }
    }
    peer->PushMessage("ssc", MASTERNODE_SYNC_LIST, nInvCount);
    LogPrint("masternode", "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, peer->GetId());
}
bool CMasternodeMan::HasRequestedMasternodeSyncTooOften(CNode* pfrom)
{
    bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());
    if (!isLocal)
    {
        if(networkMessageManager_.peerHasRequestedMasternodeListTooOften(pfrom->addr))
        {
            Misbehaving(pfrom->GetId(), 34);
            return true;
        }
    }
    return false;
}
void CMasternodeMan::ProcessMessage(CActiveMasternode& localMasternode,CMasternodeSync& masternodeSynchronization, CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Masternode Broadcast
        CMasternodeBroadcast mnb;
        vRecv >> mnb;
        if (!ProcessBroadcast(localMasternode,masternodeSynchronization,pfrom, mnb))
          return;
    }

    else if (strCommand == "mnp") { //Masternode Ping
        CMasternodePing mnp;
        vRecv >> mnp;

        LogPrint("masternode", "mnp - Masternode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());
        if (!ProcessPing(pfrom, mnp, masternodeSynchronization))
            return;
    } else if (strCommand == "dseg") { //Get Masternode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        bool peerIsRequestingMasternodeListSync = vin == CTxIn();
        if (peerIsRequestingMasternodeListSync && !HasRequestedMasternodeSyncTooOften(pfrom)) { //only should ask for this once
            SyncMasternodeListWithPeer(pfrom);
        }
        else if(!peerIsRequestingMasternodeListSync)
        {
            CMasternode* pmn = Find(vin);
            if(pmn != nullptr && NotifyPeerOfMasternode(*pmn,pfrom) )
            {
                LogPrint("masternode", "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId());
                return;
            }
        }
    }
}

void CMasternodeMan::Remove(const CTxIn& vin)
{
    LOCK(cs);

    std::vector<CMasternode>::iterator it = networkMessageManager_.masternodes.begin();
    while (it != networkMessageManager_.masternodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("masternode", "CMasternodeMan: Removing Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), networkMessageManager_.masternodeCount() - 1);
            networkMessageManager_.masternodes.erase(it);
            break;
        }
        ++it;
    }
}

void
CMasternodeMan::ResetRankingCache()
{
    rankingCache.reset(new RankingCache);
}

std::string CMasternodeMan::ToString() const
{
    std::ostringstream info;

    info << "Masternodes: "
        << (int)networkMessageManager_.masternodeCount()
        << ", "
        << networkMessageManager_.ToString();

    return info.str();
}
