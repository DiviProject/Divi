// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLETDB_H
#define BITCOIN_WALLETDB_H

#include "amount.h"
#include <Account.h>
#include "db.h"
#include <destination.h>
#include <KeyMetadata.h>
#include <script/script.h>
#include <PrivKey.h>

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
class CWalletTx;
class uint160;
class uint256;
class BlockMap;
class CChain;
class I_WalletLoader;
class CHDChain;
class CHDPubKey;
class CKeyMetadata;
using WalletTxVector = std::vector<CWalletTx>;

/** Error statuses for the wallet database */
enum DBErrors {
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE,
    DB_LOAD_OK_FIRST_RUN,
    DB_LOAD_OK_RELOAD,
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB
{
private:
    Settings& settings_;
    std::string dbFilename_;
    unsigned& walletDbUpdated_;

    static bool Recover(
        CDBEnv& dbenv,
        std::string filename,
        bool fOnlyKeys);
    static bool Recover(
        CDBEnv& dbenv,
        std::string filename);
public:

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

    bool WriteDefaultKey(const CPubKey& vchPubKey);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);
    bool WriteCryptedHDChain(const CHDChain& chain);
    bool WriteHDPubKey(const CHDPubKey& hdPubKey, const CKeyMetadata& keyMeta);

    DBErrors LoadWallet(I_WalletLoader& pwallet);

private:
    CWalletDB(const CWalletDB&) = delete;
    void operator=(const CWalletDB&) = delete;
};

void ThreadFlushWalletDB(const std::string& strWalletFile);
bool BackupWallet(const std::string& walletDBFilename, const std::string& strDest);

#endif // BITCOIN_WALLETDB_H
