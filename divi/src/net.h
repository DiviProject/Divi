// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include "bloom.h"
#include "compat.h"
#include "hash.h"
#include "limitedmap.h"
#include "mruset.h"
#include "netbase.h"
#include "protocol.h"
#include "random.h"
#include "streams.h"
#include "sync.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include <Node.h>
#include <NetworkLocalAddressHelpers.h>

#include <deque>
#include <stdint.h>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/signals2/signal.hpp>

class CAddrMan;
class CBlockIndex;
class CNode;

namespace boost
{
class thread_group;
} // namespace boost

void AddOneShot(std::string strDest);
bool RecvLine(SOCKET hSocket, std::string& strLine);
void AddressCurrentlyConnected(const CService& addr);
CNode* FindNode(const CNetAddr& ip);
CNode* FindNode(const std::string& addrName);
CNode* FindNode(const CService& ip);
CNode* ConnectNode(CAddress addrConnect, const char* pszDest = NULL, bool obfuScationMaster = false);
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant* grantOutbound = NULL, const char* strDest = NULL, bool fOneShot = false);
void MapPort(bool fUseUPnP);
bool BindListenPort(const CService& bindAddr, std::string& strError, bool fWhitelisted = false);
void StartNode(boost::thread_group& threadGroup);
bool StopNode();

CNodeSignals& GetNodeSignals();

bool IsPeerAddrLocalGood(CNode* pnode);
void AdvertizeLocal(CNode* pnode);

extern bool fDiscover;
extern bool fListen;
int GetMaxConnections();
CAddrMan& GetNetworkAddressManager();

extern std::vector<CNode*> vNodes;
extern CCriticalSection cs_vNodes;
extern std::map<CInv, CDataStream> mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;
extern limitedmap<CInv, int64_t> mapAlreadyAskedFor;

extern std::vector<std::string> vAddedNodes;
extern CCriticalSection cs_vAddedNodes;

extern NodeId nLastNodeId;
extern CCriticalSection cs_nLastNodeId;

class CExplicitNetCleanup
{
public:
    static void callCleanup();
};

class CTransaction;
void RelayTransaction(const CTransaction& tx);
void RelayTransaction(const CTransaction& tx, const CDataStream& ss);
void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll = false);
void RelayInv(CInv& inv);

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    boost::filesystem::path pathAddr;

public:
    CAddrDB();
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};

class UIMessenger;
bool AlertsAreEnabled();
bool SetNumberOfFileDescriptors(UIMessenger& uiMessenger, int& nFD);
void SetNetworkingParameters();
bool InitializeP2PNetwork(UIMessenger& uiMessenger);
#endif // BITCOIN_NET_H
