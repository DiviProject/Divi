// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLETDB_H
#define BITCOIN_WALLETDB_H

#include "amount.h"
#include <Account.h>
#include <destination.h>
#include <KeyMetadata.h>
#include <script/script.h>
#include <PrivKey.h>

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <I_WalletDatabase.h>

/** Access to the wallet database (wallet.dat) */
class CDB;
class CDBEnv;
class Settings;

class CWalletDB final: public I_WalletDatabase
{
private:
    Settings& settings_;
    const std::string dbFilename_;
    unsigned& walletDbUpdated_;
    CDBEnv& berkleyDbEnvWrapper_;
    std::unique_ptr<CDB> berkleyDB_;

    static bool Recover(
        CDBEnv& dbenv,
        std::string filename,
        bool fOnlyKeys);
    static bool Recover(
        CDBEnv& dbenv,
        std::string filename);
public:

    CWalletDB(Settings& settings,const std::string& dbFilename, const char* pszMode = "r+");
    ~CWalletDB();

    bool AtomicWriteBegin() override;
    bool AtomicWriteEnd(bool) override;

    //I_WalletDatabase
    bool WriteName(const std::string& strAddress, const std::string& strName) override;
    bool EraseName(const std::string& strAddress) override;
    bool WriteTx(uint256 hash, const CWalletTx& wtx) override;
    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta) override;
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata& keyMeta) override;
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey) override;
    bool WriteCScript(const uint160& hash, const CScript& redeemScript) override;
    bool EraseCScript(const uint160& hash) override;
    bool WriteWatchOnly(const CScript& script) override;
    bool EraseWatchOnly(const CScript& script) override;
    bool WriteMultiSig(const CScript& script) override;
    bool EraseMultiSig(const CScript& script) override;
    bool WriteBestBlock(const CBlockLocator& locator) override;
    bool ReadBestBlock(CBlockLocator& locator) override;
    bool WriteDefaultKey(const CPubKey& vchPubKey) override;
    bool ReadPool(int64_t nPool, CKeyPool& keypool) override;
    bool WritePool(int64_t nPool, const CKeyPool& keypool) override;
    bool ErasePool(int64_t nPool) override;
    bool WriteMinVersion(int nVersion) override;
    bool WriteHDChain(const CHDChain& chain) override;
    bool WriteCryptedHDChain(const CHDChain& chain) override;
    bool WriteHDPubKey(const CHDPubKey& hdPubKey, const CKeyMetadata& keyMeta) override;
    DBErrors LoadWallet(I_WalletLoader& pwallet) override;
    bool RewriteWallet() override;

    const unsigned& numberOfWalletUpdates() const;
    bool Flush();
    bool BackupWallet(const std::string& destination);

private:
    CWalletDB(const CWalletDB&) = delete;
    void operator=(const CWalletDB&) = delete;
};

void ThreadFlushWalletDB(Settings& settings, const std::string& strWalletFile);
bool BackupWallet(Settings& settings, const std::string& walletDBFilename, const std::string& strDest);

#endif // BITCOIN_WALLETDB_H
