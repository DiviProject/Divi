// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_VERIFYDB_H
#define BITCOIN_VERIFYDB_H

#include <memory>

class CCoinsView;
class CChain;
class CClientUIInterface;
class CCoinsViewCache;
class CSporkManager;
class CBlockTreeDB;
class BlockConnectionService;
class BlockDiskDataReader;
class ChainstateManager;
class I_BlockDataReader;
class MasternodeModule;
class CChainParams;

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB
{
public:
    typedef bool (*ShutdownListener)();
private:
    std::unique_ptr<const I_BlockDataReader> blockDiskReader_;
    const CCoinsView& coinView_;
    ChainstateManager& chainstate_;
    std::unique_ptr<CCoinsViewCache> coinsViewCache_;
    const CSporkManager& sporkManager_;
    std::unique_ptr<const BlockConnectionService> chainManager_;
    const CChain& activeChain_;
    CClientUIInterface& clientInterface_;
    const unsigned coinsCacheSize_;
    ShutdownListener shutdownListener_;
public:
    CVerifyDB(
        const CChainParams& chainParameters,
        const MasternodeModule& masternodeModule,
        ChainstateManager& chainstate,
        const CCoinsView& coinView,
        const CSporkManager& sporkManager,
        CClientUIInterface& clientInterface,
        const unsigned& coinsCacheSize,
        ShutdownListener shutdownListener);
    ~CVerifyDB();
    bool VerifyDB(int nCheckLevel, int nCheckDepth) const;
};

#endif
