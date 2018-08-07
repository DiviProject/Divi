//// Copyright (c) 2014-2016 The Dash developers
//// Copyright (c) 2015-2017 The PIVX Developers
//// Distributed under the MIT/X11 software license, see the accompanying
//// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
//#ifndef ACTIVEMASTERNODE_H
//#define ACTIVEMASTERNODE_H
//
//#include "init.h"
//#include "key.h"
//#include "masternode.h"
//#include "net.h"
//#include "obfuscation.h"
//#include "sync.h"
//#include "wallet.h"
//
//#define ACTIVE_MASTERNODE_INITIAL 0 // initial state
//#define ACTIVE_MASTERNODE_SYNC_IN_PROCESS 1
//#define ACTIVE_MASTERNODE_INPUT_TOO_NEW 2
//#define ACTIVE_MASTERNODE_NOT_CAPABLE 3
//#define ACTIVE_MASTERNODE_STARTED 4
//
//// Responsible for activating the Masternode and pinging the network
//class CActiveMasternode
//{
//private:
//    // critical section to protect the inner data structures
//    mutable CCriticalSection cs;
//
//    /// Ping Masternode
//    bool SendMasternodePing(std::string& errorMessage);
//
//    /// Register any Masternode
//    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyMasternode, CPubKey pubKeyMasternode, std::string& errorMessage);
//
//    /// Get 10000 DIV input that can be used for the Masternode
//    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
//    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
//
//public:
//    // Initialized by init.cpp
//    // Keys for the main Masternode
//    CPubKey pubKeyMasternode;
//
//    // Initialized while registering Masternode
//    CTxIn vin;
//    CService service;
//
//    int status;
//    std::string notCapableReason;
//
//    CActiveMasternode()
//    {
//        status = ACTIVE_MASTERNODE_INITIAL;
//    }
//
//    /// Manage status of main Masternode
//    void ManageStatus();
//    std::string GetStatus();
//
//    /// Register remote Masternode
//    bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);
//
//    /// Get 10000 DIV input that can be used for the Masternode
//    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
//    vector<COutput> SelectCoinsMasternode();
//
//    /// Enable cold wallet mode (run a Masternode with no funds)
//    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);
//};
//
//#endif
