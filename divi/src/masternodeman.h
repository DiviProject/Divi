// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include "key.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"

#include <memory>

#define MASTERNODES_DUMP_SECONDS (15 * 60)

class CMasternodeMan;
class CMasternodeSync;
class CMasternodePayments;
class CActiveMasternode;
class MasternodeNetworkMessageManager;
class CChain;
class BlockMap;
class CAddrMan;

class CMasternodeMan
{
private:
    MasternodeNetworkMessageManager& networkMessageManager_;
    // critical section to protect the inner data structures
    CCriticalSection& cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    const CChain& activeChain_;
    const BlockMap& blockIndicesByHash_;
    CAddrMan& addressManager_;

    bool Add(const CMasternode& mn);
    bool NotifyPeerOfMasternode(const CMasternode& mn, CNode* peer);
    void SyncMasternodeListWithPeer(CNode* peer);
    bool HasRequestedMasternodeSyncTooOften(CNode* pfrom);
    void Remove(const CTxIn& vin);

    bool UpdateWithNewBroadcast(const CMasternodeBroadcast &mnb, CMasternode& masternode) const;
    bool CheckInputsForMasternode(const CMasternodeBroadcast& mnb, int& nDoS);
    bool CheckAndUpdateMasternode(CMasternodeSync& masternodeSynchronization,CMasternodeBroadcast& mnb, int& nDoS);
    bool CheckAndUpdatePing(CMasternode& mn,CMasternodePing& mnp, int& nDoS, bool skipPingChainSyncCheck = false);
public:

    CMasternodeMan(
        MasternodeNetworkMessageManager& networkMessageManager,
        const CChain& activeChain,
        const BlockMap& blockIndicesByHash,
        CAddrMan& addressManager);
    CMasternodeMan(const CMasternodeMan& other) = delete;
    ~CMasternodeMan();

    /// Check all Masternodes
    void Check();
    void Check(CMasternode& mn, bool forceCheck = false);
    bool TimeSinceLastPingIsWithin(const CMasternode& mn, const int timeWindow, int64_t now = -1) const;
    bool IsTooEarlyToSendPingUpdate(const CMasternode& mn, int64_t now) const;
    bool IsTooEarlyToReceivePingUpdate(const CMasternode& mn, int64_t now) const;

    /// Check all Masternodes and remove inactive
    void CheckAndRemoveInnactive(CMasternodeSync& masternodeSynchronization,bool forceExpiredRemoval = false);

    int CountEnabled() const;

    /// Find an entry
    CMasternode* Find(const CScript& payee) = delete;
    CMasternode* Find(const CTxIn& vin);
    CMasternode* Find(const CPubKey& pubKeyMasternode);
    /** Records a ping in the list of our seen ping messages, and also updates the
     *  list of known broadcasts if the ping corresponds to one we know (i.e. updates
     *  the ping contained in the seen broadcast).
     *
     *  This method assumes that the ping has already been checked and is valid.
     */
    void RecordSeenPing(const CMasternodePing& mnp);

    /** Processes a masternode broadcast.  It is verified first, and then
     *  the masternode updated or added accordingly.
     *
     *  If pfrom is null, we assume this is a startmasternode or broadcaststartmasternode
     *  command.  Otherwise, we apply any potential DoS banscore.
     *
     *  Returns true if all was valid, and false if not.  */
    bool ProcessBroadcast(CActiveMasternode& localMasternode, CMasternodeSync& masternodeSynchronization, CNode* pfrom, CMasternodeBroadcast& mnb);

    /** Processes a masternode ping.  It is verified first, and if valid,
     *  used to update our state and inserted into mapSeenMasternodePing.
     *
     *  If pfrom is null, we assume this is from a local RPC command.  Otherwise
     *  we apply potential DoS banscores.
     *
     *  Returns true if the ping message was valid.  */
    bool ProcessPing(CNode* pfrom, CMasternodePing& mnp, CMasternodeSync& masternodeSynchronization);

    void ProcessMessage(CActiveMasternode& localMasternode, CMasternodeSync& masternodeSynchronization, CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    std::string ToString() const;
};

#endif
