// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeman.h"
#include "activemasternode.h"
#include "addrman.h"
#include "masternode.h"
#include "mruset.h"
#include "obfuscation.h"
#include "spork.h"
#include "Logging.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <main.h>

#include <array>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Maximum nCheckNum that we allow for ranking masternodes.
 *  In practice, we only need to check if a node is in the top 10
 *  (or rather, top-20 to give some leeway before punishing nodes
 *  with wrong claims).  */
static constexpr unsigned MAX_RANKING_CHECK_NUM = 20;

/** Number of entries (blocks) we keep in the cache of ranked masternodes.  */
static constexpr unsigned RANKING_CACHE_SIZE = 2500;

/** Masternode manager */
CMasternodeMan mnodeman;

//    pathMN = GetDataDir() / "mncache.dat";
//    strMagicMessage = "MasternodeCache";

namespace
{

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

CMasternodeMan::~CMasternodeMan() = default;

CMasternodeMan::CMasternodeMan(
    ): nDsqCount(0)
    , rankingCache(new RankingCache)
{
}

bool CMasternodeMan::Add(const CMasternode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CMasternode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("masternode", "CMasternodeMan: Adding new Masternode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vMasternodes.push_back(mn);
        return true;
    }

    return false;
}

void CMasternodeMan::AskForMN(CNode* pnode, const CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMasternodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMasternodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("masternode", "CMasternodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMasternodeListEntry[vin.prevout] = askAgain;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        mn.Check();
    }
}

void CMasternodeMan::CheckAndRemoveInnactive(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    std::vector<CMasternode>::iterator it = vMasternodes.begin();
    while (it != vMasternodes.end()) {
        if ((*it).activeState == CMasternode::MASTERNODE_REMOVE ||
            (*it).activeState == CMasternode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMasternode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
            LogPrint("masternode", "CMasternodeMan: Removing inactive Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
            while (it3 != mapSeenMasternodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    masternodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMasternodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this masternode again if we see another ping
            std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
            while (it2 != mWeAskedForMasternodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForMasternodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vMasternodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Masternode list
    std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMasternodeList.begin();
    while (it1 != mAskedUsForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Masternode list
    it1 = mWeAskedForMasternodeList.begin();
    while (it1 != mWeAskedForMasternodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForMasternodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Masternodes we've asked for
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForMasternodeListEntry.begin();
    while (it2 != mWeAskedForMasternodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForMasternodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMasternodeBroadcast
    std::map<uint256, CMasternodeBroadcast>::iterator it3 = mapSeenMasternodeBroadcast.begin();
    while (it3 != mapSeenMasternodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodeBroadcast.erase(it3++);
            masternodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMasternodePing
    std::map<uint256, CMasternodePing>::iterator it4 = mapSeenMasternodePing.begin();
    while (it4 != mapSeenMasternodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMasternodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    vMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nDsqCount = 0;
}

int CMasternodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMasternode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMasternode_Age = 0;

    for (auto& mn : vMasternodes) {
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
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    for (const auto& mn : vMasternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMasternodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
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

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
        std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMasternodeList.find(pnode->addr);
        if (it != mWeAskedForMasternodeList.end()) {
            if (GetTime() < (*it).second) {
                LogPrint("masternode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                return;
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;
}

CMasternode* CMasternodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CMasternode* CMasternodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CMasternode* CMasternodeMan::Find(const CPubKey& pubKeyMasternode)
{
    LOCK(cs);

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.pubKeyMasternode == pubKeyMasternode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
std::vector<CMasternode*> CMasternodeMan::GetMasternodePaymentQueue(const uint256& seedHash, const int nBlockHeight, bool fFilterSigTime)
{
    LOCK(cs);
    std::vector< CMasternode* > masternodeQueue;
    std::map<const CMasternode*, uint256> masternodeScores;

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CMasternode& mn, vMasternodes)
    {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) continue;

        // It's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        // On regtest, we ignore this criterion, because it makes it hard to do
        // proper testing with a very small number of masternodes (which would
        // be scheduled and skipped all the time).
        if (Params().NetworkID() != CBaseChainParams::REGTEST) {
            if (masternodePayments.IsScheduled(mn, nBlockHeight)) continue;
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

std::vector<CMasternode*> CMasternodeMan::GetMasternodePaymentQueue(const CBlockIndex* pindex, const int offset, bool fFilterSigTime)
{
    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindex, offset))
        return {};

    const int64_t nBlockHeight = pindex->nHeight + offset;

    return GetMasternodePaymentQueue(seedHash, nBlockHeight, fFilterSigTime);
}

CMasternode* CMasternodeMan::GetNextMasternodeInQueueForPayment(const CBlockIndex* pindex, const int offset, bool fFilterSigTime)
{
    std::vector<CMasternode*> mnQueue = GetMasternodePaymentQueue(pindex, offset, fFilterSigTime);

    return (!mnQueue.empty())? mnQueue.front() : NULL;
}

CMasternode* CMasternodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? masternodePayments.GetMinMasternodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CMasternode& mn, vMasternodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
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

        for (auto& mn : vMasternodes) {
            int64_t score;
            if (!CheckAndGetScore(mn, seedHash, minProtocol, score))
              continue;

            rankedNodes.emplace_back(score, mn.vin.prevout.hash);
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

bool CMasternodeMan::ProcessBroadcast(CNode* pfrom, CMasternodeBroadcast& mnb)
{
    if (mapSeenMasternodeBroadcast.count(mnb.GetHash())) { //seen
        masternodeSync.AddedMasternodeList(mnb.GetHash());
        return true;
    }

    const auto& mnp = mnb.lastPing;
    mnodeman.mapSeenMasternodePing.emplace(mnp.GetHash(), mnp);
    mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    int nDoS = 0;
    if (!mnb.CheckAndUpdate(nDoS)) {
        if (nDoS > 0 && pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Masternode
    //  - this is expensive, so it's only done once per Masternode
    if (!CObfuScationSigner::IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress, static_cast<MasternodeTier>(mnb.nTier))) {
        LogPrintf("%s : mnb - Got mismatched pubkey and vin\n", __func__);
        if (pfrom != nullptr)
            Misbehaving(pfrom->GetId(), 33);
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
    if (mnb.CheckInputsAndAdd(nDoS)) {
        // use this as a peer
        CNetAddr addr("127.0.0.1");
        if (pfrom != nullptr)
            addr = pfrom->addr;
        addrman.Add(CAddress(mnb.addr), addr, 2 * 60 * 60);
        masternodeSync.AddedMasternodeList(mnb.GetHash());
    } else {
        LogPrintf("%s : - Rejected Masternode entry %s\n", __func__, mnb.vin.prevout.hash.ToString());
        if (nDoS > 0 && pfrom != nullptr)
            Misbehaving(pfrom->GetId(), nDoS);
        return false;
    }

    return true;
}

void CMasternodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Masternode Broadcast
        CMasternodeBroadcast mnb;
        vRecv >> mnb;
        if (!ProcessBroadcast(pfrom, mnb))
          return;
    }

    else if (strCommand == "mnp") { //Masternode Ping
        CMasternodePing mnp;
        vRecv >> mnp;

        LogPrint("masternode", "mnp - Masternode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenMasternodePing.count(mnp.GetHash())) return; //seen
        mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Masternode list
            CMasternode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Masternode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal)
            {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMasternodeList.find(pfrom->addr);
                if (i != mAskedUsForMasternodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("%s : dseg - peer already asked me for the list\n", __func__);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CMasternode& mn, vMasternodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("masternode", "dseg - Sending Masternode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenMasternodeBroadcast.count(hash)) mapSeenMasternodeBroadcast.insert(std::make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("masternode", "dseg - Sent 1 Masternode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", MASTERNODE_SYNC_LIST, nInvCount);
            LogPrint("masternode", "dseg - Sent %d Masternode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
}

void CMasternodeMan::Remove(const CTxIn& vin)
{
    LOCK(cs);

    std::vector<CMasternode>::iterator it = vMasternodes.begin();
    while (it != vMasternodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("masternode", "CMasternodeMan: Removing Masternode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vMasternodes.erase(it);
            break;
        }
        ++it;
    }
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast mnb)
{
    LOCK(cs);
    mapSeenMasternodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMasternodeBroadcast.insert(std::make_pair(mnb.GetHash(), mnb));

    LogPrint("masternode","CMasternodeMan::UpdateMasternodeList -- masternode=%s\n", mnb.vin.prevout.ToStringShort());

    CMasternode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CMasternode mn(mnb);
        if (Add(mn)) {
            masternodeSync.AddedMasternodeList(mnb.GetHash());
        }
    } else if (pmn->UpdateFromNewBroadcast(mnb)) {
        masternodeSync.AddedMasternodeList(mnb.GetHash());
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

    info << "Masternodes: " << (int)vMasternodes.size() << ", peers who asked us for Masternode list: " << (int)mAskedUsForMasternodeList.size() << ", peers we asked for Masternode list: " << (int)mWeAskedForMasternodeList.size() << ", entries in Masternode list we asked for: " << (int)mWeAskedForMasternodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
