// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include "compat.h"
#include "netbase.h"
#include "sync.h"
#include <Node.h>
#include <NetworkLocalAddressHelpers.h>
#include <I_PeerSyncQueryService.h>
#include <I_PeerBlockNotifyService.h>

#include <deque>
#include <stdint.h>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/signals2/signal.hpp>

class CAddrMan;
class CAlert;
class CBlockIndex;
class CNode;
class CWallet;
class CInv;
class CDataStream;
class uint256;
class CNodeStats;

namespace boost
{
class thread_group;
} // namespace boost

void AddressCurrentlyConnected(const CService& addr);
CNode* ConnectNode(CAddress addrConnect, const char* pszDest = NULL, bool obfuScationMaster = false);
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant* grantOutbound = NULL, const char* strDest = NULL, bool fOneShot = false);
void StartNode(boost::thread_group& threadGroup,CWallet* pwalletMain);
bool StopNode();

CNodeSignals& GetNodeSignals();

bool PeersLocalAddressIsGood(CNode* pnode);
void AdvertizeLocal(CNode* pnode);

const bool& IsListening();
int GetMaxConnections();
CAddrMan& GetNetworkAddressManager();
const I_PeerSyncQueryService& GetPeerSyncQueryService();
const I_PeerBlockNotifyService& GetPeerBlockNotifyService();

bool RepeatRelayedInventory(CNode* pfrom, const CInv& inv);
class CTransaction;
void RelayTransaction(const CTransaction& tx);
void RelayTransaction(const CTransaction& tx, const CDataStream& ss);
void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll = false);
void RelayInv(CInv& inv);

void NotifyPeersOfNewChainTip(const int chainHeight, const uint256& updatedBlockHashForChainTip, const int fallbackPeerChainHeightEstimate);
void DeterministicallyRelayAddressToLimitedPeers(const CAddress& addr,int numberOfNodes);
void RelayAlertToPeers(const CAlert& alert);
std::vector<std::string> BanOutdatedPeers();
bool BanSpecificPeer(const CNetAddr& address);
int GetPeerCount();
int SchedulePingingPeers();
enum NodeConnectionStatus
{
    INBOUND,
    OUTBOUND,
    NOT_CONNECTED,
};
NodeConnectionStatus GetConnectionStatus(const CService& addrNode);
void GetNodeStats(std::vector<CNodeStats>& vstats);

class UIMessenger;
bool AlertsAreEnabled();
bool SetNumberOfFileDescriptors(UIMessenger& uiMessenger, int& nFD);
void SetNetworkingParameters();
bool InitializeP2PNetwork(UIMessenger& uiMessenger);
#endif // BITCOIN_NET_H
