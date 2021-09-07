// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLETDB_H
#define BITCOIN_WALLETDB_H

#include "amount.h"
#include "db.h"
#include "key.h"
#include "keystore.h"
#include <destination.h>

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

class CAccount;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;
class BlockMap;
class CChain;
using WalletTxVector = std::vector<CWalletTx>;

/** Error statuses for the wallet database */
enum DBErrors {
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

class CKeyMetadata
{
public:
    static const int CURRENT_VERSION = 1;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown

    bool unknownKeyID;
    bool isHDPubKey;
    std::string hdkeypath;
    std::string hdchainid;

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(nCreateTime);
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        unknownKeyID = true;
        isHDPubKey = false;
        hdkeypath = "";
        hdchainid = "";
    }
};

class CAddressBookData;
class I_WalletLoader
{
public:
    virtual void LoadWalletTransaction(const CWalletTx& wtxIn) = 0;
    virtual bool LoadWatchOnly(const CScript& dest) = 0;
    virtual bool LoadMinVersion(int nVersion) = 0;
    virtual bool LoadMultiSig(const CScript& dest) = 0;
    virtual bool LoadKey(const CKey& key, const CPubKey& pubkey) = 0;
    virtual bool LoadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey) = 0;
    virtual bool LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret) = 0;
    virtual bool LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata, const bool updateFirstKeyTimestamp) = 0;
    virtual bool SetDefaultKey(const CPubKey& vchPubKey, bool updateDatabase) = 0;
    virtual void LoadKeyPool(int nIndex, const CKeyPool &keypool) = 0;
    virtual bool LoadCScript(const CScript& redeemScript) = 0;
    virtual void UpdateNextTransactionIndexAvailable(int64_t transactionIndex) = 0;
    virtual bool SetHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool SetCryptedHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool LoadHDPubKey(const CHDPubKey &hdPubKey) = 0;
    virtual void ReserializeTransactions(const std::vector<uint256>& transactionIDs) = 0;
    virtual CAddressBookData& ModifyAddressBookData(const CTxDestination& address) = 0;
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB
{
private:
    Settings& settings_;
    std::string dbFilename_;
    unsigned& walletDbUpdated_;
public:
    void IncrementDBUpdateCount() const;

    CWalletDB(Settings& settings,const std::string& strFilename, const char* pszMode = "r+");

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(uint256 hash, const CWalletTx& wtx);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata& keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);
    bool EraseCScript(const uint160& hash);

    bool WriteWatchOnly(const CScript& script);
    bool EraseWatchOnly(const CScript& script);

    bool WriteMultiSig(const CScript& script);
    bool EraseMultiSig(const CScript& script);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteDefaultKey(const CPubKey& vchPubKey);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);

    DBErrors LoadWallet(I_WalletLoader* pwallet);
    static bool Recover(
        CDBEnv& dbenv,
        std::string filename,
        bool fOnlyKeys);
    static bool Recover(
        CDBEnv& dbenv,
        std::string filename);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);
    bool WriteCryptedHDChain(const CHDChain& chain);
    bool WriteHDPubKey(const CHDPubKey& hdPubKey, const CKeyMetadata& keyMeta);

    static void IncrementUpdateCounter();
    static unsigned int GetUpdateCounter();
private:
    CWalletDB(const CWalletDB&);
    void operator=(const CWalletDB&);
};

void ThreadFlushWalletDB(const std::string& strWalletFile);
bool BackupWallet(const CWallet& wallet, const std::string& strDest);

#endif // BITCOIN_WALLETDB_H
