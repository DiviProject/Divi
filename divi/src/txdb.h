// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "leveldbwrapper.h"
#include <coins.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

class uint256;
class CBlockFileInfo;
class CDiskBlockIndex;


struct CAddressIndexKey;
struct CAddressIndexIteratorKey;
struct CAddressIndexIteratorHeightKey;
struct CSpentIndexKey;
struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CDiskTxPos;
struct CCoinsStats;
struct CSpentIndexValue;
struct TxIndexEntry;
struct BlockMap;

/** CCoinsView backed by the LevelDB coin database (chainstate/) */
struct CCoinsStats {
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nSerializedSize;
    uint256 hashSerialized;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), hashBlock(0), nTransactions(0), nTransactionOutputs(0), nSerializedSize(0), hashSerialized(0), nTotalAmount(0) {}
};

class CCoinsViewDB final: public CCoinsView
{
protected:
    CLevelDBWrapper db;
    const BlockMap& blockIndicesByHash_;
public:
    CCoinsViewDB(const BlockMap& blockIndicesByHash, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetCoins(const uint256& txid, CCoins& coins) const override;
    bool HaveCoins(const uint256& txid) const override;
    uint256 GetBestBlock() const override;
    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock) override;
    bool GetStats(CCoinsStats& stats) const;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CLevelDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);

    bool addressIndexing_;
    bool spentIndexing_;
public:
    void SetAddressIndexing(bool addressIndexing);
    bool GetAddressIndexing() const;
    void SetSpentIndexing(bool spentIndexing);
    bool GetSpentIndexing() const;
    void LoadIndexingFlags();
    void WriteIndexingFlags(bool addressIndexing, bool spentIndexing);

    bool WriteBlockIndex(const CDiskBlockIndex& blockindex);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo& fileinfo) const;
    bool WriteBlockFileInfo(int nFile, const CBlockFileInfo& fileinfo);
    bool ReadLastBlockFile(int& nFile) const;
    bool WriteLastBlockFile(int nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool& fReindex) const;
    bool WriteFlag(const std::string& name, bool fValue);
    bool ReadFlag(const std::string& name, bool& fValue) const;
    bool LoadBlockIndices(BlockMap& blockIndicesByHash) const;

    bool ReadBestBlockHash(uint256& bestBlockHash) const;
    bool WriteBestBlockHash(const uint256 bestBlockHash);

    bool ReadTxIndex(const uint256& txid, CDiskTxPos& pos) const;
    bool WriteTxIndex(const std::vector<TxIndexEntry>& list);
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool ReadAddressIndex(uint160 addressHash, int type,
                          std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                          int start = 0, int end = 0) const;
    bool ReadSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value) const;
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    bool ReadAddressUnspentIndex(uint160 addressHash, int type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect) const;
};

#endif // BITCOIN_TXDB_H
