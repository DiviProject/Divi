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
#include <NodeSignals.h>
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
class CInv;
class CDataStream;
class uint256;
class CNodeStats;
class CNodeStateStats;

namespace boost
{
class thread_group;
} // namespace boost

bool CheckNodeIsAcceptingConnections(CAddress addrToConnectTo);
bool OpenNetworkConnection(const CAddress& addrConnect, const char* strDest = NULL, bool fOneShot = false);
bool addNode(const std::string& strNode, const std::string& strCommand);
std::vector<std::string> getAddedNodeList();

void StartNode(boost::thread_group& threadGroup);
bool StopNode();
void CleanupP2PConnections();

CAddrMan& GetNetworkAddressManager();
CNodeSignals& GetNodeSignals();
void RegisterNodeSignals(CNodeSignals& nodeSignals);
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

bool PeersLocalAddressIsGood(CNode* pnode);
void AdvertizeLocal(CNode* pnode);

int GetMaxConnections();
const I_PeerSyncQueryService& GetPeerSyncQueryService();
const I_PeerBlockNotifyService& GetPeerBlockNotifyService();

bool RepeatRelayedInventory(CNode* pfrom, const CInv& inv);
class CTransaction;
void RelayTransactionToAllPeers(const CTransaction& tx);
void RelayInv(CInv& inv);

void NotifyPeersOfNewChainTip(const int chainHeight, const uint256& updatedBlockHashForChainTip, const int fallbackPeerChainHeightEstimate);
void DeterministicallyRelayAddressToLimitedPeers(const CAddress& addr,int numberOfNodes);
void RelayAlertToPeers(const CAlert& alert);
void RelayAllAlertsTo(CNode* peer);
std::vector<std::string> BanOutdatedPeers();
bool BanSpecificPeer(const CNetAddr& address);
int GetPeerCount();
void SchedulePingingPeers();
enum NodeConnectionStatus
{
    INBOUND,
    OUTBOUND,
    NOT_CONNECTED,
};
NodeConnectionStatus GetConnectionStatus(const CService& addrNode);

struct CNodeStateStats {
    bool stateFound;
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
    CNodeStateStats(
        ): stateFound(false)
        , nMisbehavior(0)
        , nSyncHeight(-1)
        , nCommonHeight(-1)
        , vHeightInFlight()
    {
    }
};
void GetNodeStateStats(std::vector<std::pair<CNodeStats,CNodeStateStats>>& vstats);

class UIMessenger;
bool AlertsAreEnabled();
bool SetNumberOfFileDescriptors(UIMessenger& uiMessenger, int& nFD);
void SetNetworkingParameters();
bool InitializeP2PNetwork(UIMessenger& uiMessenger);
#endif // BITCOIN_NET_H
