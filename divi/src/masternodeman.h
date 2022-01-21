// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include <memory>
#include <string>

#define MASTERNODES_DUMP_SECONDS (15 * 60)

class CPubKey;
class CMasternode;
class CMasternodeBroadcast;
class CMasternodePing;
class CTxIn;
class CDataStream;
class CMasternodeMan;
class CMasternodeSync;
class CMasternodePayments;
class CActiveMasternode;
class MasternodeNetworkMessageManager;
class CChain;
class BlockMap;
class CAddrMan;
class CNode;

class CMasternodeMan
{
private:
    MasternodeNetworkMessageManager& networkMessageManager_;
    CMasternodeSync& masternodeSynchronization_;

    const CChain& activeChain_;
    const BlockMap& blockIndicesByHash_;
    CAddrMan& addressManager_;
    CActiveMasternode& localActiveMasternode_;

    bool Add(const CMasternode& mn);
    void Remove(const CTxIn& vin);

    enum MnUpdateStatus
    {
        MN_NOT_FOUND,
        MN_UPDATE_INVALID,
        MN_CANT_BE_UPDATED,
        MN_UPDATED,
    };
    bool UpdateWithNewBroadcast(const CMasternodeBroadcast &mnb, CMasternode& masternode) const;
    bool CheckInputsForMasternode(const CMasternodeBroadcast& mnb);
    bool CheckMasternodeBroadcastContext(CMasternodeBroadcast& mnb, int& nDoS);
    MnUpdateStatus UpdateMasternodeFromBroadcast(CMasternodeBroadcast& mnb);
    bool CheckAndUpdatePing(CMasternode& mn, const CMasternodePing& mnp, int& nDoS, bool skipPingChainSyncCheck = false);

    /** Processes a masternode broadcast.  It is verified first, and then
     *  the masternode updated or added accordingly.
     *
     *  If pfrom is null, we assume this is a startmasternode or broadcaststartmasternode
     *  command.  Otherwise, we apply any potential DoS banscore.
     *
     *  Returns true if all was valid, and false if not.  */
    bool ProcessBroadcast(CNode* pfrom, CMasternodeBroadcast& mnb);

    /** Processes a masternode ping.  It is verified first, and if valid,
     *  used to update our state and inserted into mapSeenMasternodePing.
     *
     *  If pfrom is null, we assume this is from a local RPC command.  Otherwise
     *  we apply potential DoS banscores.
     *
     *  Returns true if the ping message was valid.  */
    bool ProcessPing(CNode* pfrom, const CMasternodePing& mnp);

    CMasternode* Find(const CTxIn& vin);
    CMasternode* Find(const CPubKey& pubKeyMasternode);
public:

    CMasternodeMan(
        MasternodeNetworkMessageManager& networkMessageManager,
        CMasternodeSync& masternodeSynchronization,
        const CChain& activeChain,
        const BlockMap& blockIndicesByHash,
        CAddrMan& addressManager,
        CActiveMasternode& localActiveMasternode);
    CMasternodeMan(const CMasternodeMan& other) = delete;
    ~CMasternodeMan();

    /// Check all Masternodes
    void Check();
    void Check(CMasternode& mn, bool forceCheck = false);

    /// Find a thread safe copy of mn
    bool GetMNCopy(const CTxIn& vin, CMasternode& mn);

    bool ProcessMNBroadcastsAndPings(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    void ManageLocalMasternode();
    std::string ToString() const;
};

#endif
