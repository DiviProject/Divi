#include <net_processing_divi.h>

#include <spork.h>
#include <netmessagemaker.h>
#include <map>
#include <functional>
#include <init.h>
#include <boost/thread.hpp>
#include <masternodes/masternode-payments.h>
#include <masternodes/masternode-sync.h>
#include <masternodes/masternodeman.h>
#include <masternodes/masternode.h>

namespace LegacyInvMsg {
enum {
    MSG_TX = 1,
    MSG_BLOCK,
    // Nodes may always request a MSG_FILTERED_BLOCK in a getdata, however,
    // MSG_FILTERED_BLOCK should not appear in any invs except as a part of getdata.
    MSG_FILTERED_BLOCK,
    MSG_TXLOCK_REQUEST,
    MSG_TXLOCK_VOTE,
    MSG_SPORK,
    MSG_MASTERNODE_WINNER,
    MSG_MASTERNODE_SCANNING_ERROR,
    MSG_BUDGET_VOTE,
    MSG_BUDGET_PROPOSAL,
    MSG_BUDGET_FINALIZED,
    MSG_BUDGET_FINALIZED_VOTE,
    MSG_MASTERNODE_QUORUM,
    MSG_MASTERNODE_ANNOUNCE,
    MSG_MASTERNODE_PING
};
}

using SporkHandler = std::function<CSerializedNetMsg(const CNetMsgMaker &, const uint256 &)>;
using MapSporkHandlers = std::map<int, SporkHandler>;

#define ADD_HANDLER(sporkID, handler) sporkHandlers.emplace(sporkID, [](const CNetMsgMaker &msgMaker, const uint256 &hash) -> CSerializedNetMsg handler)

static const MapSporkHandlers &GetMapGetDataHandlers()
{
    static MapSporkHandlers sporkHandlers;

    if(sporkHandlers.empty())
    {
        ADD_HANDLER(MSG_SPORK, {
                        if(mapSporks.count(hash)) {
                            return msgMaker.Make(NetMsgType::SPORK, mapSporks[hash]);
                        }
                        return {};
                    });
//        ADD_HANDLER(MSG_TXLOCK_REQUEST, {
//                        CTxLockRequestRef txLockRequest;
//                        if(instantsend.GetTxLockRequest(hash, txLockRequest)) {
//                            return msgMaker.Make(NetMsgType::TXLOCKREQUEST, *txLockRequest);
//                        }
//                        return {};
//                    });
//        ADD_HANDLER(MSG_TXLOCK_VOTE, {
//                        CTxLockVote vote;
//                        if(instantsend.GetTxLockVote(hash, vote)) {
//                            return msgMaker.Make(NetMsgType::TXLOCKVOTE, vote);
//                        }
//                        return {};
//                    });
//        ADD_HANDLER(MSG_MASTERNODE_PAYMENT_BLOCK, {
//                        BlockMap::iterator mi = mapBlockIndex.find(hash);
//                        LOCK(cs_mapMasternodeBlocks);
//                        if (mi != mapBlockIndex.end() && mnpayments.mapMasternodeBlocks.count(mi->second->nHeight)) {
//                            for(const CMasternodePayee& payee : mnpayments.mapMasternodeBlocks[mi->second->nHeight].vecPayees) {
//                                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
//                                for(const uint256& hash : vecVoteHashes) {
//                                    if(mnpayments.HasVerifiedPaymentVote(hash)) {
//                                        return msgMaker.Make(NetMsgType::MASTERNODEPAYMENTVOTE, mnpayments.mapMasternodePaymentVotes[hash]);
//                                    }
//                                }
//                            }
//                        }
//                        return {};
//                    });
        ADD_HANDLER(MSG_MASTERNODE_WINNER, {
                        if(masternodePayments.mapMasternodePayeeVotes.count(hash)) {
                            return msgMaker.Make(NetMsgType::MASTERNODEPAYMENTVOTE, masternodePayments.mapMasternodePayeeVotes[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_ANNOUNCE, {
                        if(mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
                            return msgMaker.Make(NetMsgType::MNANNOUNCE, mnodeman.mapSeenMasternodeBroadcast[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_PING, {
                        if(mnodeman.mapSeenMasternodePing.count(hash)) {
                            return msgMaker.Make(NetMsgType::MNPING, mnodeman.mapSeenMasternodePing[hash]);
                        }
                        return {};
                    });
    }

    return sporkHandlers;
}

bool net_processing_divi::ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams, CConnman *connman, const CInv &inv)
{
    const auto &handlersMap = GetMapGetDataHandlers();
    auto it = handlersMap.find(inv.type);
    if(it != std::end(handlersMap))
    {
        const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
        auto &&msg = it->second(msgMaker, inv.hash);
        if(!msg.command.empty())
        {
            connman->PushMessage(pfrom, std::move(msg));
            return true;
        }
    }

    return false;
}

void net_processing_divi::ProcessExtension(CNode *pfrom, CValidationState &state, const std::string &strCommand, CDataStream &vRecv, CConnman *connman)
{
    mnodeman.ProcessMessage(pfrom, state, strCommand, vRecv, *connman);
    masternodePayments.ProcessMessageMasternodePayments(pfrom, state, strCommand, vRecv, *connman);
    sporkManager.ProcessSpork(pfrom, state, strCommand, vRecv, connman);
    masternodeSync.ProcessMessage(pfrom, state, strCommand, vRecv, *connman);
}

void net_processing_divi::ThreadProcessExtensions(CConnman *pConnman)
{
#if 0

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the PrivateSend thread
    RenameThread("xsn-ps");

    unsigned int nTick = 0;

    auto &connman = *pConnman;
    while (!ShutdownRequested())
    {
        boost::this_thread::interruption_point();
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        masternodeSync.ProcessTick(connman);
        merchantnodeSync.ProcessTick(connman);

        if(!ShutdownRequested()) {

            nTick++;

            if(masternodeSync.IsBlockchainSynced()) {
                // make sure to check all masternodes first
                mnodeman.Check();

                // check if we should activate or ping every few minutes,
                // slightly postpone first run to give net thread a chance to connect to some peers
                if(nTick % MASTERNODE_MIN_MNP_SECONDS == 15)
                    activeMasternode.ManageState(connman);

                if(nTick % 60 == 0) {
                    mnodeman.ProcessMasternodeConnections(connman);
                    mnodeman.CheckAndRemove(connman);
                    mnpayments.CheckAndRemove();
                    instantsend.CheckAndRemove();
                }
                if(fMasterNode && (nTick % (60 * 5) == 0)) {
                    mnodeman.DoFullVerificationStep(connman);
                }

                if(nTick % (60 * 5) == 0) {
                    governance.DoMaintenance(connman);
                }
            }

            if(merchantnodeSync.IsBlockchainSynced()) {

                merchantnodeman.Check();
                if(nTick % MERCHANTNODE_MIN_MNP_SECONDS == 15)
                    activeMerchantnode.ManageState(connman);

                if(nTick % 60 == 0) {
                    merchantnodeman.ProcessMerchantnodeConnections(connman);
                    merchantnodeman.CheckAndRemove(connman);
                }
                if(fMerchantNode && (nTick % (60 * 5) == 0)) {
                    merchantnodeman.DoFullVerificationStep(connman);
                }
            }
        }
    }
#endif
}


bool net_processing_divi::AlreadyHave(const CInv &inv)
{
    switch(inv.type)
    {
    /*
    Divi Related Inventory Messages

    --

    We shouldn't update the sync times for each of the messages when we already have it.
    We're going to be asking many nodes upfront for the full inventory list, so we'll get duplicates of these.
    We want to only update the time on new hits, so that we can time out appropriately if needed.
    */
//    case MSG_TXLOCK_REQUEST:
//        return instantsend.AlreadyHave(inv.hash);

//    case MSG_TXLOCK_VOTE:
//        return instantsend.AlreadyHave(inv.hash);

    case MSG_SPORK:
        return mapSporks.count(inv.hash);

    case MSG_MASTERNODE_WINNER:
        return masternodePayments.mapMasternodePayeeVotes.count(inv.hash);

    case MSG_MASTERNODE_ANNOUNCE:
        return mnodeman.mapSeenMasternodeBroadcast.count(inv.hash);

    case MSG_MASTERNODE_PING:
        return mnodeman.mapSeenMasternodePing.count(inv.hash);
    }
    return true;
}

static int MapLegacyToCurrent(int nLegacyType)
{
    switch(nLegacyType)
    {
    case LegacyInvMsg::MSG_TXLOCK_REQUEST: return MSG_TXLOCK_REQUEST;
    case LegacyInvMsg::MSG_TXLOCK_VOTE: return MSG_TXLOCK_VOTE;
    case LegacyInvMsg::MSG_SPORK: return MSG_SPORK;
    case LegacyInvMsg::MSG_MASTERNODE_WINNER: return MSG_MASTERNODE_WINNER;
    case LegacyInvMsg::MSG_MASTERNODE_ANNOUNCE: return MSG_MASTERNODE_ANNOUNCE;
    case LegacyInvMsg::MSG_MASTERNODE_PING: return MSG_MASTERNODE_PING;
    }

    return nLegacyType;
}

static int MapCurrentToLegacy(int nCurrentType)
{
    switch(nCurrentType)
    {
    case MSG_TXLOCK_REQUEST: return LegacyInvMsg::MSG_TXLOCK_REQUEST;
    case MSG_TXLOCK_VOTE: return LegacyInvMsg::MSG_TXLOCK_VOTE;
    case MSG_SPORK: return LegacyInvMsg::MSG_SPORK;
    case MSG_MASTERNODE_ANNOUNCE: return LegacyInvMsg::MSG_MASTERNODE_ANNOUNCE;
    case MSG_MASTERNODE_PING: return LegacyInvMsg::MSG_MASTERNODE_PING;
    case MSG_MASTERNODE_WINNER: return LegacyInvMsg::MSG_MASTERNODE_WINNER;
    }

    return nCurrentType;
}

bool net_processing_divi::TransformInvForLegacyVersion(CInv &inv, CNode *pfrom, bool fForSending)
{

    if(pfrom->GetSendVersion() == PRESEGWIT_VERSION)
    {
//        LogPrint(BCLog::NET, "Before %d, send version: %d, recv version: %d\n", inv.type, pfrom->GetSendVersion(), pfrom->GetRecvVersion());
        if(fForSending)
            inv.type = MapCurrentToLegacy(inv.type);
        else
            inv.type = MapLegacyToCurrent(inv.type);

//        LogPrint(BCLog::NET, "After %d, send version: %d, recv version: %d\n", inv.type, pfrom->GetSendVersion(), pfrom->GetRecvVersion());
    }

    return true;
}
