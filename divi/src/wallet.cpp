// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"

#include "accumulators.h"
#include "base58.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "kernel.h"
#include "masternode-budget.h"
#include "net.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sign.h"
#include "spork.h"
#include "swifttx.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"

#include "denomination_functions.h"
#include "libzerocoin/Denominations.h"
#include <assert.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem/operations.hpp>

using namespace std;

/**
 * Settings
 */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;
unsigned int nTxConfirmTarget = 1;
bool bSpendZeroConfChange = true;
bool bdisableSystemnotifications = false; // Those bubbles can be annoying and slow down the UI when you get lots of trx
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;

/**
 * Fees smaller than this (in duffs) are considered zero fee (for transaction creation)
 * We are ~100 times smaller then bitcoin now (2015-06-23), set minTxFee 10 times higher
 * so it's still 10 times lower comparing to bitcoin.
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(10000);
int64_t nStartupTime = GetAdjustedTime();

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly {
    bool operator()(const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t1,
        const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return NULL;
    return &(it->second);
}

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet);                                 // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    RandAddSeedPerfmon();
    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey& pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey& vchPubKey,
    const vector<unsigned char>& vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey,
                vchCryptedSecret,
                mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE) {
        std::string strAddr = CBitcoinAddress(CScriptID(redeemScript)).ToString();
        LogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript& dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript& dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::AddMultiSig(const CScript& dest)
{
    if (!CCryptoKeyStore::AddMultiSig(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information
    NotifyMultiSigChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveMultiSig(dest))
        return false;
    if (!HaveMultiSig())
        NotifyMultiSigChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseMultiSig(dest))
            return false;

    return true;
}

bool CWallet::LoadMultiSig(const CScript& dest)
{
    return CCryptoKeyStore::AddMultiSig(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool anonymizeOnly)
{
    SecureString strWalletPassphraseFinal;

    if (!IsLocked()) {
        fWalletUnlockAnonymizeOnly = anonymizeOnly;
        return true;
    }

    strWalletPassphraseFinal = strWalletPassphrase;


    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH (const MasterKeyMap::value_type& pMasterKey, mapMasterKeys) {
            if (!crypter.SetKeyFromPassphrase(strWalletPassphraseFinal, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(vMasterKey)) {
                fWalletUnlockAnonymizeOnly = anonymizeOnly;
                return true;
            }
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();
    SecureString strOldWalletPassphraseFinal = strOldWalletPassphrase;

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        BOOST_FOREACH (MasterKeyMap::value_type& pMasterKey, mapMasterKeys) {
            if (!crypter.SetKeyFromPassphrase(strOldWalletPassphraseFinal, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey)) {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();

                return true;
            }
        }
    }

    return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteBestBlock(loc);
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
        nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked) {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx& wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    BOOST_FOREACH (const CTxIn& txin, wtx.vin) {
        if (mapTxSpends.count(txin.prevout) <= 1 || wtx.IsZerocoinSpend())
            continue; // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
            result.insert(it->second);
    }
    return result;
}

void CWallet::SyncMetaData(pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = NULL;
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const uint256& hash = it->second;
        int n = mapWallet[hash].nOrderPos;
        if (n < nMinOrderPos) {
            nMinOrderPos = n;
            copyFrom = &mapWallet[hash];
        }
    }
    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const uint256& hash = it->second;
        CWalletTx* copyTo = &mapWallet[hash];
        if (copyFrom == copyTo) continue;
        copyTo->mapValue = copyFrom->mapValue;
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        copyTo->strFromAccount = copyFrom->strFromAccount;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0)
            return true; // Spent
    }
    return false;
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(make_pair(outpoint, wtxid));
    pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData(range);
}


void CWallet::AddToSpends(const uint256& wtxid)
{
    assert(mapWallet.count(wtxid));
    CWalletTx& thisTx = mapWallet[wtxid];
    if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    BOOST_FOREACH (const CTxIn& txin, thisTx.vin)
        AddToSpends(txin.prevout, wtxid);
}

bool CWallet::GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_10000);
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if (strTxHash.empty()) // No output specified, select the first one
        return GetVinAndKeysFromOutput(vPossibleCoins[0], txinRet, pubKeyRet, keyRet);

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);

    int nOutputIndex;
    try {
        nOutputIndex = std::stoi(strOutputIndex.c_str());
    } catch (const std::exception& e) {
        LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
        return false;
    }

    BOOST_FOREACH (COutput& out, vPossibleCoins)
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex) // found it!
            return GetVinAndKeysFromOutput(out, txinRet, pubKeyRet, keyRet);

    LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate specified masternode vin\n");
    return false;
}

bool CWallet::GetVinAndKeysFromOutput(COutput out, CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    txinRet = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!GetKey(keyID, keyRet)) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;
    RandAddSeedPerfmon();

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;
    RandAddSeedPerfmon();

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked) {
            assert(!pwalletdbEncryption);
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin()) {
                delete pwalletdbEncryption;
                pwalletdbEncryption = NULL;
                return false;
            }
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey)) {
            if (fFileBacked) {
                pwalletdbEncryption->TxnAbort();
                delete pwalletdbEncryption;
            }
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload their unencrypted wallet.
            assert(false);
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked) {
            if (!pwalletdbEncryption->TxnCommit()) {
                delete pwalletdbEncryption;
                // We now have keys encrypted in memory, but not on disk...
                // die to avoid confusion and let the user reload their unencrypted wallet.
                assert(false);
            }

            delete pwalletdbEncryption;
            pwalletdbEncryption = NULL;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);
    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB* pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
        CWalletTx* wtx = &((*it).second);
        txOrdered.insert(make_pair(wtx->nOrderPos, TxPair(wtx, (CAccountingEntry*)0)));
    }
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH (CAccountingEntry& entry, acentries) {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }

    return txOrdered;
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        BOOST_FOREACH (PAIRTYPE(const uint256, CWalletTx) & item, mapWallet)
            item.second.MarkDirty();
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet)
{
    uint256 hash = wtxIn.GetHash();

    if (fFromLoadWallet) {
        mapWallet[hash] = wtxIn;
        mapWallet[hash].BindWallet(this);
        AddToSpends(hash);
    } else {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        bool fInsertedNew = ret.second;
        if (fInsertedNew) {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext();

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (wtxIn.hashBlock != 0) {
                if (mapBlockIndex.count(wtxIn.hashBlock)) {
                    int64_t latestNow = wtx.nTimeReceived;
                    int64_t latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        std::list<CAccountingEntry> acentries;
                        TxItems txOrdered = OrderedTxItems(acentries);
                        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
                            CWalletTx* const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry* const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx) {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            } else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated) {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    int64_t blocktime = mapBlockIndex[wtxIn.hashBlock]->GetBlockTime();
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                } else
                    LogPrintf("AddToWallet() : found %s in block %s not in index\n",
                        wtxIn.GetHash().ToString(),
                        wtxIn.hashBlock.ToString());
            }
            AddToSpends(hash);
        }

        bool fUpdated = false;
        if (!fInsertedNew) {
            // Merge
            if (wtxIn.hashBlock != 0 && wtxIn.hashBlock != wtx.hashBlock) {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex)) {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe) {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk())
                return false;

        // Break debit/credit balance caches:
        wtx.MarkDirty();

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if (!strCmd.empty()) {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }
    }
    return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate)
{
    {
        AssertLockHeld(cs_wallet);
        bool fExisted = mapWallet.count(tx.GetHash()) != 0;
        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || IsFromMe(tx)) {
            CWalletTx wtx(this, tx);
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(*pblock);
            return AddToWallet(wtx);
        }
    }
    return false;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    LOCK2(cs_main, cs_wallet);
    if (!AddToWalletIfInvolvingMe(tx, pblock, true))
        return; // Not one of ours

    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    BOOST_FOREACH (const CTxIn& txin, tx.vin) {
        if (!tx.IsZerocoinSpend() && mapWallet.count(txin.prevout.hash))
            mapWallet[txin.prevout.hash].MarkDirty();
    }
}

void CWallet::EraseFromWallet(const uint256& hash)
{
    if (!fFileBacked)
        return;
    {
        LOCK(cs_wallet);
        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseTx(hash);
    }
    return;
}


isminetype CWallet::IsMine(const CTxIn& txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end()) {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                return IsMine(prev.vout[txin.prevout.n]);
        }
    }
    return ISMINE_NO;
}

bool CWallet::IsMyZerocoinSpend(const CBigNum& bnSerial) const
{
    return CWalletDB(strWalletFile).ReadZerocoinSpendSerialEntry(bnSerial);
}

CAmount CWallet::GetDebit(const CTxIn& txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end()) {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                if (IsMine(prev.vout[txin.prevout.n]) & filter)
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

// Recursively determine the rounds of a given input (How deep is the Obfuscation chain for a given input)
int CWallet::GetRealInputObfuscationRounds(CTxIn in, int rounds) const
{
    static std::map<uint256, CMutableTransaction> mDenomWtxes;

    if (rounds >= 16) return 15; // 16 rounds max

    uint256 hash = in.prevout.hash;
    unsigned int nout = in.prevout.n;

    const CWalletTx* wtx = GetWalletTx(hash);
    if (wtx != NULL) {
        std::map<uint256, CMutableTransaction>::const_iterator mdwi = mDenomWtxes.find(hash);
        // not known yet, let's add it
        if (mdwi == mDenomWtxes.end()) {
            LogPrint("obfuscation", "GetInputObfuscationRounds INSERTING %s\n", hash.ToString());
            mDenomWtxes[hash] = CMutableTransaction(*wtx);
        }
        // found and it's not an initial value, just return it
        else if (mDenomWtxes[hash].vout[nout].nRounds != -10) {
            return mDenomWtxes[hash].vout[nout].nRounds;
        }


        // bounds check
        if (nout >= wtx->vout.size()) {
            // should never actually hit this
            LogPrint("obfuscation", "GetInputObfuscationRounds UPDATED   %s %3d %3d\n", hash.ToString(), nout, -4);
            return -4;
        }

        if (pwalletMain->IsCollateralAmount(wtx->vout[nout].nValue)) {
            mDenomWtxes[hash].vout[nout].nRounds = -3;
            LogPrint("obfuscation", "GetInputObfuscationRounds UPDATED   %s %3d %3d\n", hash.ToString(), nout, mDenomWtxes[hash].vout[nout].nRounds);
            return mDenomWtxes[hash].vout[nout].nRounds;
        }

        //make sure the final output is non-denominate
        if (/*rounds == 0 && */ !IsDenominatedAmount(wtx->vout[nout].nValue)) //NOT DENOM
        {
            mDenomWtxes[hash].vout[nout].nRounds = -2;
            LogPrint("obfuscation", "GetInputObfuscationRounds UPDATED   %s %3d %3d\n", hash.ToString(), nout, mDenomWtxes[hash].vout[nout].nRounds);
            return mDenomWtxes[hash].vout[nout].nRounds;
        }

        bool fAllDenoms = true;
        BOOST_FOREACH (CTxOut out, wtx->vout) {
            fAllDenoms = fAllDenoms && IsDenominatedAmount(out.nValue);
        }
        // this one is denominated but there is another non-denominated output found in the same tx
        if (!fAllDenoms) {
            mDenomWtxes[hash].vout[nout].nRounds = 0;
            LogPrint("obfuscation", "GetInputObfuscationRounds UPDATED   %s %3d %3d\n", hash.ToString(), nout, mDenomWtxes[hash].vout[nout].nRounds);
            return mDenomWtxes[hash].vout[nout].nRounds;
        }

        int nShortest = -10; // an initial value, should be no way to get this by calculations
        bool fDenomFound = false;
        // only denoms here so let's look up
        BOOST_FOREACH (CTxIn in2, wtx->vin) {
            if (IsMine(in2)) {
                int n = GetRealInputObfuscationRounds(in2, rounds + 1);
                // denom found, find the shortest chain or initially assign nShortest with the first found value
                if (n >= 0 && (n < nShortest || nShortest == -10)) {
                    nShortest = n;
                    fDenomFound = true;
                }
            }
        }
        mDenomWtxes[hash].vout[nout].nRounds = fDenomFound ? (nShortest >= 15 ? 16 : nShortest + 1) // good, we a +1 to the shortest one but only 16 rounds max allowed
                                                             :
                                                             0; // too bad, we are the fist one in that chain
        LogPrint("obfuscation", "GetInputObfuscationRounds UPDATED   %s %3d %3d\n", hash.ToString(), nout, mDenomWtxes[hash].vout[nout].nRounds);
        return mDenomWtxes[hash].vout[nout].nRounds;
    }

    return rounds - 1;
}

// respect current settings
int CWallet::GetInputObfuscationRounds(CTxIn in) const
{
    LOCK(cs_wallet);
    int realObfuscationRounds = GetRealInputObfuscationRounds(in, 0);
    return realObfuscationRounds > nZeromintPercentage ? nZeromintPercentage : realObfuscationRounds;
}

bool CWallet::IsDenominated(const CTxIn& txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end()) {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size()) return IsDenominatedAmount(prev.vout[txin.prevout.n].nValue);
        }
    }
    return false;
}

bool CWallet::IsDenominated(const CTransaction& tx) const
{
    /*
        Return false if ANY inputs are non-denom
    */
    bool ret = true;
    BOOST_FOREACH (const CTxIn& txin, tx.vin) {
        if (!IsDenominated(txin)) {
            ret = false;
        }
    }
    return ret;
}


bool CWallet::IsDenominatedAmount(CAmount nInputAmount) const
{
    BOOST_FOREACH (CAmount d, obfuScationDenominations)
        if (nInputAmount == d)
            return true;
    return false;
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey)) {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int64_t CWalletTx::GetComputedTxTime() const
{
    int64_t nTime = GetTxTime();
    if (IsZerocoinSpend() || IsZerocoinMint()) {
        if (IsInMainChain())
            return mapBlockIndex.at(hashBlock)->GetBlockTime();
        else
            return nTimeReceived;
    }
    return nTime;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase()) {
            // Generated block
            if (hashBlock != 0) {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        } else {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end()) {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && hashBlock != 0) {
                    map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                    if (mi != pwallet->mapRequestCount.end())
                        nRequests = (*mi).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

void CWalletTx::GetAmounts(list<COutputEntry>& listReceived,
    list<COutputEntry>& listSent,
    CAmount& nFee,
    string& strSentAccount,
    const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee:
    CAmount nDebit = GetDebit(filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nValueOut = GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    for (unsigned int i = 0; i < vout.size(); ++i) {
        const CTxOut& txout = vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0) {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        } else if (!(fIsMine & filter) && !IsZerocoinSpend())
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;
        if (txout.scriptPubKey.IsZerocoinMint()) {
            address = CNoDestination();
        } else if (!ExtractDestination(txout.scriptPubKey, address)) {
            if (!IsCoinStake() && !IsCoinBase()) {
                LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n", this->GetHash().ToString());
            }
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }
}

void CWalletTx::GetAccountAmounts(const string& strAccount, CAmount& nReceived, CAmount& nSent, CAmount& nFee, const isminefilter& filter) const
{
    nReceived = nSent = nFee = 0;

    CAmount allFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount) {
        BOOST_FOREACH (const COutputEntry& s, listSent)
            nSent += s.amount;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        BOOST_FOREACH (const COutputEntry& r, listReceived) {
            if (pwallet->mapAddressBook.count(r.destination)) {
                map<CTxDestination, CAddressBookData>::const_iterator mi = pwallet->mapAddressBook.find(r.destination);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second.name == strAccount)
                    nReceived += r.amount;
            } else if (strAccount.empty()) {
                nReceived += r.amount;
            }
        }
    }
}


bool CWalletTx::WriteToDisk()
{
    return CWalletDB(pwallet->strWalletFile).WriteTx(GetHash(), *this);
}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;
    int64_t nNow = GetTime();

    CBlockIndex* pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);

        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)))
            pindex = chainActive.Next(pindex);

        ShowProgress(_("Rescanning..."), 0); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        double dProgressStart = Checkpoints::GuessVerificationProgress(pindex, false);
        double dProgressTip = Checkpoints::GuessVerificationProgress(chainActive.Tip(), false);
        while (pindex) {
            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
                ShowProgress(_("Rescanning..."), std::max(1, std::min(99, (int)((Checkpoints::GuessVerificationProgress(pindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100))));

            CBlock block;
            ReadBlockFromDisk(block, pindex);
            BOOST_FOREACH (CTransaction& tx, block.vtx) {
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate))
                    ret++;
            }
            pindex = chainActive.Next(pindex);
            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, Checkpoints::GuessVerificationProgress(pindex));
            }
        }
        ShowProgress(_("Rescanning..."), 100); // hide progress dialog in GUI
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH (PAIRTYPE(const uint256, CWalletTx) & item, mapWallet) {
        const uint256& wtxid = item.first;
        CWalletTx& wtx = item.second;
        assert(wtx.GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain();

        if (!wtx.IsCoinBase() && nDepth < 0) {
            // Try to add to memory pool
            LOCK(mempool.cs);
            wtx.AcceptToMemoryPool(false);
        }
    }
}

bool CWalletTx::InMempool() const
{
    LOCK(mempool.cs);
    if (mempool.exists(GetHash())) {
        return true;
    }
    return false;
}

void CWalletTx::RelayWalletTransaction(std::string strCommand)
{
    if (!IsCoinBase()) {
        if (GetDepthInMainChain() == 0) {
            uint256 hash = GetHash();
            LogPrintf("Relaying wtx %s\n", hash.ToString());

            if (strCommand == "ix") {
                mapTxLockReq.insert(make_pair(hash, (CTransaction) * this));
                CreateNewLock(((CTransaction) * this));
                RelayTransactionLockReq((CTransaction) * this, true);
            } else {
                RelayTransaction((CTransaction) * this);
            }
        }
    }
}

set<uint256> CWalletTx::GetConflicts() const
{
    set<uint256> result;
    if (pwallet != NULL) {
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

void CWallet::ResendWalletTransactions()
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nTimeBestReceived < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast any of our txes that aren't in a block yet
    LogPrintf("ResendWalletTransactions()\n");
    {
        LOCK(cs_wallet);
        // Sort them in chronological order
        multimap<unsigned int, CWalletTx*> mapSorted;
        BOOST_FOREACH (PAIRTYPE(const uint256, CWalletTx) & item, mapWallet) {
            CWalletTx& wtx = item.second;
            // Don't rebroadcast until it's had plenty of time that
            // it should have gotten in already by now.
            if (nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60)
                mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
        }
        BOOST_FOREACH (PAIRTYPE(const unsigned int, CWalletTx*) & item, mapSorted) {
            CWalletTx& wtx = *item.second;
            wtx.RelayWalletTransaction();
        }
    }
}

/** @} */ // end of mapWallet


/** @defgroup Actions
 *
 * @{
 */

CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetZerocoinBalance(bool fMatureOnly) const
{
    CAmount nTotal = 0;
    //! zerocoin specific fields
    std::map<libzerocoin::CoinDenomination, unsigned int> myZerocoinSupply;
    for (auto& denom : libzerocoin::zerocoinDenomList) {
        myZerocoinSupply.insert(make_pair(denom, 0));
    }

    {
        LOCK2(cs_main, cs_wallet);
        // Get Unused coins
        list<CZerocoinMint> listPubCoin = CWalletDB(strWalletFile).ListMintedCoins(true, fMatureOnly, true);
        for (auto& mint : listPubCoin) {
            libzerocoin::CoinDenomination denom = mint.GetDenomination();
            nTotal += libzerocoin::ZerocoinDenominationToAmount(denom);
            myZerocoinSupply.at(denom)++;
        }
    }
    for (auto& denom : libzerocoin::zerocoinDenomList) {
        LogPrint("zero","%s My coins for denomination %d pubcoin %s\n", __func__,denom, myZerocoinSupply.at(denom));
    }
    LogPrint("zero","Total value of coins %d\n",nTotal);

    if (nTotal < 0 ) nTotal = 0; // Sanity never hurts

    return nTotal;
}

CAmount CWallet::GetImmatureZerocoinBalance() const
{
    return GetZerocoinBalance(false) - GetZerocoinBalance(true);
}

CAmount CWallet::GetUnconfirmedZerocoinBalance() const
{
    CAmount nUnconfirmed = 0;
    CWalletDB walletdb(pwalletMain->strWalletFile);
    list<CZerocoinMint> listMints = walletdb.ListMintedCoins(true, false, true);
 
    std::map<libzerocoin::CoinDenomination, int> mapUnconfirmed;
    for (const auto& denom : libzerocoin::zerocoinDenomList){
        mapUnconfirmed.insert(make_pair(denom, 0));
    }

    {
        LOCK2(cs_main, cs_wallet);
        for (auto& mint : listMints){
            if (!mint.GetHeight() || mint.GetHeight() > chainActive.Height() - Params().Zerocoin_MintRequiredConfirmations()) {
                libzerocoin::CoinDenomination denom = mint.GetDenomination();
                nUnconfirmed += libzerocoin::ZerocoinDenominationToAmount(denom);
                mapUnconfirmed.at(denom)++;
            }
        }
    }

    for (auto& denom : libzerocoin::zerocoinDenomList) {
        LogPrint("zero","%s My unconfirmed coins for denomination %d pubcoin %s\n", __func__,denom, mapUnconfirmed.at(denom));
    }

    LogPrint("zero","Total value of unconfirmed coins %ld\n", nUnconfirmed);

    if (nUnconfirmed < 0 ) nUnconfirmed = 0; // Sanity never hurts

    return nUnconfirmed;
}

CAmount CWallet::GetUnlockedCoins() const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0)
                nTotal += pcoin->GetUnlockedCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetLockedCoins() const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0)
                nTotal += pcoin->GetLockedCredit();
        }
    }

    return nTotal;
}

// Get a Map pairing the Denominations with the amount of Zerocoin for each Denomination
std::map<libzerocoin::CoinDenomination, CAmount> CWallet::GetMyZerocoinDistribution() const
{
    std::map<libzerocoin::CoinDenomination, CAmount> spread;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        spread.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, 0));
    {
        LOCK2(cs_main, cs_wallet);
        list<CZerocoinMint> listPubCoin = CWalletDB(strWalletFile).ListMintedCoins(true, true, true);
        for (auto& mint : listPubCoin)
            spread.at(mint.GetDenomination())++;
    }
    return spread;
}


CAmount CWallet::GetAnonymizableBalance() const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAnonymizableCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetAnonymizedBalance() const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAnonymizedCredit();
        }
    }

    return nTotal;
}

// Note: calculated including unconfirmed,
// that's ok as long as we use it for informational purposes only
double CWallet::GetAverageAnonymizedRounds() const
{
    if (fLiteMode) return 0;

    double fTotal = 0;
    double fCount = 0;

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            uint256 hash = (*it).first;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                CTxIn vin = CTxIn(hash, i);

                if (IsSpent(hash, i) || IsMine(pcoin->vout[i]) != ISMINE_SPENDABLE || !IsDenominated(vin)) continue;

                int rounds = GetInputObfuscationRounds(vin);
                fTotal += (float)rounds;
                fCount += 1;
            }
        }
    }

    if (fCount == 0) return 0;

    return fTotal / fCount;
}

// Note: calculated including unconfirmed,
// that's ok as long as we use it for informational purposes only
CAmount CWallet::GetNormalizedAnonymizedBalance() const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            uint256 hash = (*it).first;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                CTxIn vin = CTxIn(hash, i);

                if (IsSpent(hash, i) || IsMine(pcoin->vout[i]) != ISMINE_SPENDABLE || !IsDenominated(vin)) continue;
                if (pcoin->GetDepthInMainChain() < 0) continue;

                int rounds = GetInputObfuscationRounds(vin);
                nTotal += pcoin->vout[i].nValue * rounds / nZeromintPercentage;
            }
        }
    }

    return nTotal;
}

CAmount CWallet::GetDenominatedBalance(bool unconfirmed) const
{
    if (fLiteMode) return 0;

    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;

            nTotal += pcoin->GetDenominatedCredit(unconfirmed);
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureWatchOnlyCredit();
        }
    }
    return nTotal;
}

/**
 * populate vCoins with vector of available COutputs.
 */
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl* coinControl, bool fIncludeZeroValue, AvailableCoinsType nCoinType, bool fUseIX) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const uint256& wtxid = it->first;
            const CWalletTx* pcoin = &(*it).second;

            if (!CheckFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain(false);
            // do not use IX for inputs that have less then 6 blockchain confirmations
            if (fUseIX && nDepth < 6)
                continue;

            // We should not consider coins which aren't at least in our mempool
            // It's possible for these to be conflicted via ancestors which we may never be able to detect
            if (nDepth == 0 && !pcoin->InMempool())
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                bool found = false;
                if (nCoinType == ONLY_DENOMINATED) {
                    found = IsDenominatedAmount(pcoin->vout[i].nValue);
                } else if (nCoinType == ONLY_NOT10000IFMN) {
                    found = !(fMasterNode && pcoin->vout[i].nValue == 10000 * COIN);
                } else if (nCoinType == ONLY_NONDENOMINATED_NOT10000IFMN) {
                    if (IsCollateralAmount(pcoin->vout[i].nValue)) continue; // do not use collateral amounts
                    found = !IsDenominatedAmount(pcoin->vout[i].nValue);
                    if (found && fMasterNode) found = pcoin->vout[i].nValue != 10000 * COIN; // do not use Hot MN funds
                } else if (nCoinType == ONLY_10000) {
                    found = pcoin->vout[i].nValue == 10000 * COIN;
                } else {
                    found = true;
                }
                if (!found) continue;

                if (nCoinType == STAKABLE_COINS) {
                    if (pcoin->vout[i].IsZerocoinMint())
                        continue;
                }

                isminetype mine = IsMine(pcoin->vout[i]);
                if (IsSpent(wtxid, i))
                    continue;
                if (mine == ISMINE_NO)
                    continue;
                if (mine == ISMINE_WATCH_ONLY)
                    continue;

                if (IsLockedCoin((*it).first, i) && nCoinType != ONLY_10000)
                    continue;
                if (pcoin->vout[i].nValue <= 0 && !fIncludeZeroValue)
                    continue;
                if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs && !coinControl->IsSelected((*it).first, i))
                    continue;

                bool fIsSpendable = false;
                if ((mine & ISMINE_SPENDABLE) != ISMINE_NO)
                    fIsSpendable = true;
                if ((mine & ISMINE_MULTISIG) != ISMINE_NO)
                    fIsSpendable = true;
                vCoins.emplace_back(COutput(pcoin, i, nDepth, fIsSpendable));
            }
        }
    }
}

map<CBitcoinAddress, vector<COutput> > CWallet::AvailableCoinsByAddress(bool fConfirmed, CAmount maxCoinValue)
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, fConfirmed);

    map<CBitcoinAddress, vector<COutput> > mapCoins;
    BOOST_FOREACH (COutput out, vCoins) {
        if (maxCoinValue > 0 && out.tx->vout[out.i].nValue > maxCoinValue)
            continue;

        CTxDestination address;
        if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
            continue;

        mapCoins[CBitcoinAddress(address)].push_back(out);
    }

    return mapCoins;
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx*, unsigned int> > > vValue, const CAmount& nTotalLower, const CAmount& nTargetValue, vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++) {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++) {
            for (unsigned int i = 0; i < vValue.size(); i++) {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand() & 1 : !vfIncluded[i]) {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue) {
                        fReachedTarget = true;
                        if (nTotal < nBest) {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}


// TODO: find appropriate place for this sort function
// move denoms down
bool less_then_denom(const COutput& out1, const COutput& out2)
{
    const CWalletTx* pcoin1 = out1.tx;
    const CWalletTx* pcoin2 = out2.tx;

    bool found1 = false;
    bool found2 = false;
    BOOST_FOREACH (CAmount d, obfuScationDenominations) // loop through predefined denoms
    {
        if (pcoin1->vout[out1.i].nValue == d) found1 = true;
        if (pcoin2->vout[out2.i].nValue == d) found2 = true;
    }
    return (!found1 && found2);
}

bool CWallet::SelectStakeCoins(std::set<std::pair<const CWalletTx*, unsigned int> >& setCoins, CAmount nTargetAmount) const
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, NULL, false, STAKABLE_COINS);
    CAmount nAmountSelected = 0;

    for (const COutput& out : vCoins) {
        //make sure not to outrun target amount
        if (nAmountSelected + out.tx->vout[out.i].nValue > nTargetAmount)
            continue;

        //if zerocoinspend, then use the block time
        int64_t nTxTime = out.tx->GetTxTime();
        if (out.tx->IsZerocoinSpend()) {
            if (!out.tx->IsInMainChain())
                continue;
            nTxTime = mapBlockIndex.at(out.tx->hashBlock)->GetBlockTime();
        }

        //check for min age
        if (GetAdjustedTime() - nTxTime < nStakeMinAge)
            continue;

        //check that it is matured
        if (out.nDepth < (out.tx->IsCoinStake() ? Params().COINBASE_MATURITY() : 10))
            continue;

        //add to our stake set
        setCoins.insert(make_pair(out.tx, out.i));
        nAmountSelected += out.tx->vout[out.i].nValue;
    }
    return true;
}

bool CWallet::MintableCoins()
{
    CAmount nBalance = GetBalance();
    if (mapArgs.count("-reservebalance") && !ParseMoney(mapArgs["-reservebalance"], nReserveBalance))
        return error("MintableCoins() : invalid reserve balance amount");
    if (nBalance <= nReserveBalance)
        return false;

    vector<COutput> vCoins;
    AvailableCoins(vCoins, true);

    for (const COutput& out : vCoins) {
        int64_t nTxTime = out.tx->GetTxTime();
        if (out.tx->IsZerocoinSpend()) {
            if (!out.tx->IsInMainChain())
                continue;
            nTxTime = mapBlockIndex.at(out.tx->hashBlock)->GetBlockTime();
        }

        if (GetAdjustedTime() - nTxTime > nStakeMinAge)
            return true;
    }

    return false;
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins, set<pair<const CWalletTx*, unsigned int> >& setCoinsRet, CAmount& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<CAmount, pair<const CWalletTx*, unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = NULL;
    vector<pair<CAmount, pair<const CWalletTx*, unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    // move denoms down on the list
    sort(vCoins.begin(), vCoins.end(), less_then_denom);

    // try to find nondenom first to prevent unneeded spending of mixed coins
    for (unsigned int tryDenom = 0; tryDenom < 2; tryDenom++) {
        if (fDebug) LogPrint("selectcoins", "tryDenom: %d\n", tryDenom);
        vValue.clear();
        nTotalLower = 0;
        BOOST_FOREACH (const COutput& output, vCoins) {
            if (!output.fSpendable)
                continue;

            const CWalletTx* pcoin = output.tx;

            //            if (fDebug) LogPrint("selectcoins", "value %s confirms %d\n", FormatMoney(pcoin->vout[output.i].nValue), output.nDepth);
            if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs))
                continue;

            int i = output.i;
            CAmount n = pcoin->vout[i].nValue;
            if (tryDenom == 0 && IsDenominatedAmount(n)) continue; // we don't want denom values on first run

            pair<CAmount, pair<const CWalletTx*, unsigned int> > coin = make_pair(n, make_pair(pcoin, i));

            if (n == nTargetValue) {
                setCoinsRet.insert(coin.second);
                nValueRet += coin.first;
                return true;
            } else if (n < nTargetValue + CENT) {
                vValue.push_back(coin);
                nTotalLower += n;
            } else if (n < coinLowestLarger.first) {
                coinLowestLarger = coin;
            }
        }

        if (nTotalLower == nTargetValue) {
            for (unsigned int i = 0; i < vValue.size(); ++i) {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }
            return true;
        }

        if (nTotalLower < nTargetValue) {
            if (coinLowestLarger.second.first == NULL) // there is no input larger than nTargetValue
            {
                if (tryDenom == 0)
                    // we didn't look at denom yet, let's do it
                    continue;
                else
                    // we looked at everything possible and didn't find anything, no luck
                    return false;
            }
            setCoinsRet.insert(coinLowestLarger.second);
            nValueRet += coinLowestLarger.first;
            return true;
        }

        // nTotalLower > nTargetValue
        break;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest)) {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    } else {
        string s = "CWallet::SelectCoinsMinConf best subset: ";
        for (unsigned int i = 0; i < vValue.size(); i++) {
            if (vfBest[i]) {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
                s += FormatMoney(vValue[i].first) + " ";
            }
        }
        LogPrintf("%s - total %s\n", s, FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTx*, unsigned int> >& setCoinsRet, CAmount& nValueRet, const CCoinControl* coinControl, AvailableCoinsType coin_type, bool useIX) const
{
    // Note: this function should never be used for "always free" tx types like dstx

    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl, false, coin_type, useIX);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected()) {
        BOOST_FOREACH (const COutput& out, vCoins) {
            if (!out.fSpendable)
                continue;

            if (coin_type == ONLY_DENOMINATED) {
                CTxIn vin = CTxIn(out.tx->GetHash(), out.i);
                int rounds = GetInputObfuscationRounds(vin);
                // make sure it's actually anonymized
                if (rounds < nZeromintPercentage) continue;
            }

            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.i));
        }
        return (nValueRet >= nTargetValue);
    }

    //if we're doing only denominated, we need to round up to the nearest .1 DIV
    if (coin_type == ONLY_DENOMINATED) {
        // Make outputs by looping through denominations, from large to small
        BOOST_FOREACH (CAmount v, obfuScationDenominations) {
            BOOST_FOREACH (const COutput& out, vCoins) {
                if (out.tx->vout[out.i].nValue == v                                               //make sure it's the denom we're looking for
                    && nValueRet + out.tx->vout[out.i].nValue < nTargetValue + (0.1 * COIN) + 100 //round the amount up to .1 DIV over
                    ) {
                    CTxIn vin = CTxIn(out.tx->GetHash(), out.i);
                    int rounds = GetInputObfuscationRounds(vin);
                    // make sure it's actually anonymized
                    if (rounds < nZeromintPercentage) continue;
                    nValueRet += out.tx->vout[out.i].nValue;
                    setCoinsRet.insert(make_pair(out.tx, out.i));
                }
            }
        }
        return (nValueRet >= nTargetValue);
    }

    return (SelectCoinsMinConf(nTargetValue, 1, 6, vCoins, setCoinsRet, nValueRet) ||
            SelectCoinsMinConf(nTargetValue, 1, 1, vCoins, setCoinsRet, nValueRet) ||
            (bSpendZeroConfChange && SelectCoinsMinConf(nTargetValue, 0, 1, vCoins, setCoinsRet, nValueRet)));
}

struct CompareByPriority {
    bool operator()(const COutput& t1,
        const COutput& t2) const
    {
        return t1.Priority() > t2.Priority();
    }
};

bool CWallet::SelectCoinsByDenominations(int nDenom, CAmount nValueMin, CAmount nValueMax, std::vector<CTxIn>& vCoinsRet, std::vector<COutput>& vCoinsRet2, CAmount& nValueRet, int nObfuscationRoundsMin, int nObfuscationRoundsMax)
{
    vCoinsRet.clear();
    nValueRet = 0;

    vCoinsRet2.clear();
    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, NULL, ONLY_DENOMINATED);

    std::random_shuffle(vCoins.rbegin(), vCoins.rend());

    //keep track of each denomination that we have
    bool fFound10000 = false;
    bool fFound1000 = false;
    bool fFound100 = false;
    bool fFound10 = false;
    bool fFound1 = false;
    bool fFoundDot1 = false;

    //Check to see if any of the denomination are off, in that case mark them as fulfilled
    if (!(nDenom & (1 << 0))) fFound10000 = true;
    if (!(nDenom & (1 << 1))) fFound1000 = true;
    if (!(nDenom & (1 << 2))) fFound100 = true;
    if (!(nDenom & (1 << 3))) fFound10 = true;
    if (!(nDenom & (1 << 4))) fFound1 = true;
    if (!(nDenom & (1 << 5))) fFoundDot1 = true;

    BOOST_FOREACH (const COutput& out, vCoins) {
        // masternode-like input should not be selected by AvailableCoins now anyway
        //if(out.tx->vout[out.i].nValue == 10000*COIN) continue;
        if (nValueRet + out.tx->vout[out.i].nValue <= nValueMax) {
            bool fAccepted = false;

            // Function returns as follows:
            //
            // bit 0 - 10000 DIV+1 ( bit on if present )
            // bit 1 - 1000 DIV+1
            // bit 2 - 100 DIV+1
            // bit 3 - 10 DIV+1
            // bit 4 - 1 DIV+1
            // bit 5 - .1 DIV+1

            CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

            int rounds = GetInputObfuscationRounds(vin);
            if (rounds >= nObfuscationRoundsMax) continue;
            if (rounds < nObfuscationRoundsMin) continue;

            if (fFound10000 && fFound1000 && fFound100 && fFound10 && fFound1 && fFoundDot1) { //if fulfilled
                //we can return this for submission
                if (nValueRet >= nValueMin) {
                    //random reduce the max amount we'll submit for anonymity
                    nValueMax -= (rand() % (nValueMax / 5));
                    //on average use 50% of the inputs or less
                    int r = (rand() % (int)vCoins.size());
                    if ((int)vCoinsRet.size() > r) return true;
                }
                //Denomination criterion has been met, we can take any matching denominations
                if ((nDenom & (1 << 0)) && out.tx->vout[out.i].nValue == ((10000 * COIN) + 10000000)) {
                    fAccepted = true;
                } else if ((nDenom & (1 << 1)) && out.tx->vout[out.i].nValue == ((1000 * COIN) + 1000000)) {
                    fAccepted = true;
                } else if ((nDenom & (1 << 2)) && out.tx->vout[out.i].nValue == ((100 * COIN) + 100000)) {
                    fAccepted = true;
                } else if ((nDenom & (1 << 3)) && out.tx->vout[out.i].nValue == ((10 * COIN) + 10000)) {
                    fAccepted = true;
                } else if ((nDenom & (1 << 4)) && out.tx->vout[out.i].nValue == ((1 * COIN) + 1000)) {
                    fAccepted = true;
                } else if ((nDenom & (1 << 5)) && out.tx->vout[out.i].nValue == ((.1 * COIN) + 100)) {
                    fAccepted = true;
                }
            } else {
                //Criterion has not been satisfied, we will only take 1 of each until it is.
                if ((nDenom & (1 << 0)) && out.tx->vout[out.i].nValue == ((10000 * COIN) + 10000000)) {
                    fAccepted = true;
                    fFound10000 = true;
                } else if ((nDenom & (1 << 1)) && out.tx->vout[out.i].nValue == ((1000 * COIN) + 1000000)) {
                    fAccepted = true;
                    fFound1000 = true;
                } else if ((nDenom & (1 << 2)) && out.tx->vout[out.i].nValue == ((100 * COIN) + 100000)) {
                    fAccepted = true;
                    fFound100 = true;
                } else if ((nDenom & (1 << 3)) && out.tx->vout[out.i].nValue == ((10 * COIN) + 10000)) {
                    fAccepted = true;
                    fFound10 = true;
                } else if ((nDenom & (1 << 4)) && out.tx->vout[out.i].nValue == ((1 * COIN) + 1000)) {
                    fAccepted = true;
                    fFound1 = true;
                } else if ((nDenom & (1 << 5)) && out.tx->vout[out.i].nValue == ((.1 * COIN) + 100)) {
                    fAccepted = true;
                    fFoundDot1 = true;
                }
            }
            if (!fAccepted) continue;

            vin.prevPubKey = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey
            nValueRet += out.tx->vout[out.i].nValue;
            vCoinsRet.push_back(vin);
            vCoinsRet2.push_back(out);
        }
    }

    return (nValueRet >= nValueMin && fFound10000 && fFound1000 && fFound100 && fFound10 && fFound1 && fFoundDot1);
}

bool CWallet::SelectCoinsDark(CAmount nValueMin, CAmount nValueMax, std::vector<CTxIn>& setCoinsRet, CAmount& nValueRet, int nObfuscationRoundsMin, int nObfuscationRoundsMax) const
{
    CCoinControl* coinControl = NULL;

    setCoinsRet.clear();
    nValueRet = 0;

    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl, nObfuscationRoundsMin < 0 ? ONLY_NONDENOMINATED_NOT10000IFMN : ONLY_DENOMINATED);

    set<pair<const CWalletTx*, unsigned int> > setCoinsRet2;

    //order the array so largest nondenom are first, then denominations, then very small inputs.
    sort(vCoins.rbegin(), vCoins.rend(), CompareByPriority());

    BOOST_FOREACH (const COutput& out, vCoins) {
        //do not allow inputs less than 1 CENT
        if (out.tx->vout[out.i].nValue < CENT) continue;
        //do not allow collaterals to be selected
        if (IsCollateralAmount(out.tx->vout[out.i].nValue)) continue;
        if (fMasterNode && out.tx->vout[out.i].nValue == 10000 * COIN) continue; //masternode input

        if (nValueRet + out.tx->vout[out.i].nValue <= nValueMax) {
            CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

            int rounds = GetInputObfuscationRounds(vin);
            if (rounds >= nObfuscationRoundsMax) continue;
            if (rounds < nObfuscationRoundsMin) continue;

            vin.prevPubKey = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.push_back(vin);
            setCoinsRet2.insert(make_pair(out.tx, out.i));
        }
    }

    // if it's more than min, we're good to return
    if (nValueRet >= nValueMin) return true;

    return false;
}

bool CWallet::SelectCoinsCollateral(std::vector<CTxIn>& setCoinsRet, CAmount& nValueRet) const
{
    vector<COutput> vCoins;

    //LogPrintf(" selecting coins for collateral\n");
    AvailableCoins(vCoins);

    //LogPrintf("found coins %d\n", (int)vCoins.size());

    set<pair<const CWalletTx*, unsigned int> > setCoinsRet2;

    BOOST_FOREACH (const COutput& out, vCoins) {
        // collateral inputs will always be a multiple of DARSEND_COLLATERAL, up to five
        if (IsCollateralAmount(out.tx->vout[out.i].nValue)) {
            CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

            vin.prevPubKey = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.push_back(vin);
            setCoinsRet2.insert(make_pair(out.tx, out.i));
            return true;
        }
    }

    return false;
}

int CWallet::CountInputsWithAmount(CAmount nInputAmount)
{
    CAmount nTotal = 0;
    {
        LOCK(cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted()) {
                int nDepth = pcoin->GetDepthInMainChain(false);

                for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                    COutput out = COutput(pcoin, i, nDepth, true);
                    CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

                    if (out.tx->vout[out.i].nValue != nInputAmount) continue;
                    if (!IsDenominatedAmount(pcoin->vout[i].nValue)) continue;
                    if (IsSpent(out.tx->GetHash(), i) || IsMine(pcoin->vout[i]) != ISMINE_SPENDABLE || !IsDenominated(vin)) continue;

                    nTotal++;
                }
            }
        }
    }

    return nTotal;
}

bool CWallet::HasCollateralInputs(bool fOnlyConfirmed) const
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, fOnlyConfirmed);

    int nFound = 0;
    BOOST_FOREACH (const COutput& out, vCoins)
        if (IsCollateralAmount(out.tx->vout[out.i].nValue)) nFound++;

    return nFound > 0;
}

bool CWallet::IsCollateralAmount(CAmount nInputAmount) const
{
    return nInputAmount != 0 && nInputAmount % OBFUSCATION_COLLATERAL == 0 && nInputAmount < OBFUSCATION_COLLATERAL * 5 && nInputAmount > OBFUSCATION_COLLATERAL;
}

bool CWallet::CreateCollateralTransaction(CMutableTransaction& txCollateral, std::string& strReason)
{
    /*
        To doublespend a collateral transaction, it will require a fee higher than this. So there's
        still a significant cost.
    */
    CAmount nFeeRet = 1 * COIN;

    txCollateral.vin.clear();
    txCollateral.vout.clear();

    CReserveKey reservekey(this);
    CAmount nValueIn2 = 0;
    std::vector<CTxIn> vCoinsCollateral;

    if (!SelectCoinsCollateral(vCoinsCollateral, nValueIn2)) {
        strReason = "Error: Obfuscation requires a collateral transaction and could not locate an acceptable input!";
        return false;
    }

    // make our change address
    CScript scriptChange;
    CPubKey vchPubKey;
    assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
    scriptChange = GetScriptForDestination(vchPubKey.GetID());
    reservekey.KeepKey();

    BOOST_FOREACH (CTxIn v, vCoinsCollateral)
        txCollateral.vin.push_back(v);

    if (nValueIn2 - OBFUSCATION_COLLATERAL - nFeeRet > 0) {
        //pay collateral charge in fees
        CTxOut vout3 = CTxOut(nValueIn2 - OBFUSCATION_COLLATERAL, scriptChange);
        txCollateral.vout.push_back(vout3);
    }

    int vinNumber = 0;
    BOOST_FOREACH (CTxIn v, txCollateral.vin) {
        if (!SignSignature(*this, v.prevPubKey, txCollateral, vinNumber, int(SIGHASH_ALL | SIGHASH_ANYONECANPAY))) {
            BOOST_FOREACH (CTxIn v, vCoinsCollateral)
                UnlockCoin(v.prevout);

            strReason = "CObfuscationPool::Sign - Unable to sign collateral transaction! \n";
            return false;
        }
        vinNumber++;
    }

    return true;
}

//bool CWallet::GetBudgetSystemCollateralTX(CTransaction& tx, uint256 hash, bool useIX)
//{
//    CWalletTx wtx;
//    if (GetBudgetSystemCollateralTX(wtx, hash, useIX)) {
//        tx = (CTransaction)wtx;
//        return true;
//    }
//    return false;
//}
//
//bool CWallet::GetBudgetSystemCollateralTX(CWalletTx& tx, uint256 hash, bool useIX)
//{
//    // make our change address
//    CReserveKey reservekey(pwalletMain);
//
//    CScript scriptChange;
//    scriptChange << OP_RETURN << ToByteVector(hash);
//
//    CAmount nFeeRet = 0;
//    std::string strFail = "";
//    vector<pair<CScript, CAmount> > vecSend;
//    vecSend.push_back(make_pair(scriptChange, BUDGET_FEE_TX));
//
//    CCoinControl* coinControl = NULL;
//    bool success = CreateTransaction(vecSend, tx, reservekey, nFeeRet, strFail, coinControl, ALL_COINS, useIX, (CAmount)0);
//    if (!success) {
//        LogPrintf("GetBudgetSystemCollateralTX: Error - %s\n", strFail);
//        return false;
//    }
//
//    return true;
//}


bool CWallet::ConvertList(std::vector<CTxIn> vCoins, std::vector<CAmount>& vecAmounts)
{
    BOOST_FOREACH (CTxIn i, vCoins) {
        if (mapWallet.count(i.prevout.hash)) {
            CWalletTx& wtx = mapWallet[i.prevout.hash];
            if (i.prevout.n < wtx.vout.size()) {
                vecAmounts.push_back(wtx.vout[i.prevout.n].nValue);
            }
        } else {
            LogPrintf("ConvertList -- Couldn't find transaction\n");
        }
    }
    return true;
}

bool CWallet::CreateTransaction(const vector<pair<CScript, CAmount> >& vecSend,
    CWalletTx& wtxNew,
    CReserveKey& reservekey,
    CAmount& nFeeRet,
    std::string& strFailReason,
    const CCoinControl* coinControl,
    AvailableCoinsType coin_type,
    bool useIX,
    CAmount nFeePay)
{
    if (useIX && nFeePay < CENT) nFeePay = CENT;

    CAmount nValue = 0;

    BOOST_FOREACH (const PAIRTYPE(CScript, CAmount) & s, vecSend) {
        if (nValue < 0) {
            strFailReason = _("Transaction amounts must be positive");
            return false;
        }
        nValue += s.second;
    }
    if (vecSend.empty() || nValue < 0) {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    CMutableTransaction txNew;

    {
        LOCK2(cs_main, cs_wallet);
        {
            nFeeRet = 0;
            if (nFeePay > 0) nFeeRet = nFeePay;
            while (true) {
                txNew.vin.clear();
                txNew.vout.clear();
                wtxNew.fFromMe = true;

                CAmount nTotalValue = nValue + nFeeRet;
                double dPriority = 0;

                // vouts to the payees
                if (coinControl && !coinControl->fSplitBlock) {
                    BOOST_FOREACH (const PAIRTYPE(CScript, CAmount) & s, vecSend) {
                        CTxOut txout(s.second, s.first);
                        if (txout.IsDust(::minRelayTxFee)) {
                            strFailReason = _("Transaction amount too small");
                            return false;
                        }
                        txNew.vout.push_back(txout);
                    }
                } else //UTXO Splitter Transaction
                {
                    int nSplitBlock;

                    if (coinControl)
                        nSplitBlock = coinControl->nSplitBlock;
                    else
                        nSplitBlock = 1;

                    BOOST_FOREACH (const PAIRTYPE(CScript, CAmount) & s, vecSend) {
                        for (int i = 0; i < nSplitBlock; i++) {
                            if (i == nSplitBlock - 1) {
                                uint64_t nRemainder = s.second % nSplitBlock;
                                txNew.vout.push_back(CTxOut((s.second / nSplitBlock) + nRemainder, s.first));
                            } else
                                txNew.vout.push_back(CTxOut(s.second / nSplitBlock, s.first));
                        }
                    }
                }

                // Choose coins to use
                set<pair<const CWalletTx*, unsigned int> > setCoins;
                CAmount nValueIn = 0;

                if (!SelectCoins(nTotalValue, setCoins, nValueIn, coinControl, coin_type, useIX)) {
                    if (coin_type == ALL_COINS) {
                        strFailReason = _("Insufficient funds.");
                    } else if (coin_type == ONLY_NOT10000IFMN) {
                        strFailReason = _("Unable to locate enough funds for this transaction that are not equal 10000 DIV.");
                    } else if (coin_type == ONLY_NONDENOMINATED_NOT10000IFMN) {
                        strFailReason = _("Unable to locate enough Obfuscation non-denominated funds for this transaction that are not equal 10000 DIV.");
                    } else {
                        strFailReason = _("Unable to locate enough Obfuscation denominated funds for this transaction.");
                        strFailReason += " " + _("Obfuscation uses exact denominated amounts to send funds, you might simply need to anonymize some more coins.");
                    }

                    if (useIX) {
                        strFailReason += " " + _("SwiftX requires inputs with at least 6 confirmations, you might need to wait a few minutes and try again.");
                    }

                    return false;
                }


                BOOST_FOREACH (PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins) {
                    CAmount nCredit = pcoin.first->vout[pcoin.second].nValue;
                    //The coin age after the next block (depth+1) is used instead of the current,
                    //reflecting an assumption the user would accept a bit more delay for
                    //a chance at a free transaction.
                    //But mempool inputs might still be in the mempool, so their age stays 0
                    int age = pcoin.first->GetDepthInMainChain();
                    if (age != 0)
                        age += 1;
                    dPriority += (double)nCredit * age;
                }

                CAmount nChange = nValueIn - nValue - nFeeRet;

                //over pay for denominated transactions
                if (coin_type == ONLY_DENOMINATED) {
                    nFeeRet += nChange;
                    nChange = 0;
                    wtxNew.mapValue["DS"] = "1";
                }

                if (nChange > 0) {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-divi-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        scriptChange = GetScriptForDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        assert(ret); // should never fail, as we just unlocked

                        scriptChange = GetScriptForDestination(vchPubKey.GetID());
                    }

                    CTxOut newTxOut(nChange, scriptChange);

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (newTxOut.IsDust(::minRelayTxFee)) {
                        nFeeRet += nChange;
                        nChange = 0;
                        reservekey.ReturnKey();
                    } else {
                        // Insert change txn at random position:
                        vector<CTxOut>::iterator position = txNew.vout.begin() + GetRandInt(txNew.vout.size() + 1);
                        txNew.vout.insert(position, newTxOut);
                    }
                } else
                    reservekey.ReturnKey();

                // Fill vin
                BOOST_FOREACH (const PAIRTYPE(const CWalletTx*, unsigned int) & coin, setCoins)
                    txNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));

                // Sign
                int nIn = 0;
                BOOST_FOREACH (const PAIRTYPE(const CWalletTx*, unsigned int) & coin, setCoins)
                    if (!SignSignature(*this, *coin.first, txNew, nIn++)) {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    }

                // Embed the constructed transaction data in wtxNew.
                *static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew);

                // Limit size
                unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
                if (nBytes >= MAX_STANDARD_TX_SIZE) {
                    strFailReason = _("Transaction too large");
                    return false;
                }
                dPriority = wtxNew.ComputePriority(dPriority, nBytes);

                // Can we complete this as a free transaction?
                if (fSendFreeTransactions && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE) {
                    // Not enough fee: enough priority?
                    double dPriorityNeeded = mempool.estimatePriority(nTxConfirmTarget);
                    // Not enough mempool history to estimate: use hard-coded AllowFree.
                    if (dPriorityNeeded <= 0 && AllowFree(dPriority))
                        break;

                    // Small enough, and priority high enough, to send for free
                    if (dPriorityNeeded > 0 && dPriority >= dPriorityNeeded)
                        break;
                }

                CAmount nFeeNeeded = max(nFeePay, GetMinimumFee(nBytes, nTxConfirmTarget, mempool));

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes)) {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded) // Done, enough fee included
                    break;

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }
    }
    return true;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, const CAmount& nValue, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet, std::string& strFailReason, const CCoinControl* coinControl, AvailableCoinsType coin_type, bool useIX, CAmount nFeePay)
{
    vector<pair<CScript, CAmount> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    return CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, strFailReason, coinControl, coin_type, useIX, nFeePay);
}

// ppcoin: create coin stake transaction
bool CWallet::CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval, CMutableTransaction& txNew, unsigned int& nTxNewTime)
{
    // The following split & combine thresholds are important to security
    // Should not be adjusted if you don't understand the consequences
    //int64_t nCombineThreshold = 0;

    txNew.vin.clear();
    txNew.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    CAmount nBalance = GetBalance();

    if (mapArgs.count("-reservebalance") && !ParseMoney(mapArgs["-reservebalance"], nReserveBalance))
        return error("CreateCoinStake : invalid reserve balance amount");

    if (nBalance <= nReserveBalance)
        return false;

    // presstab HyperStake - Initialize as static and don't update the set on every run of CreateCoinStake() in order to lighten resource use
    static std::set<pair<const CWalletTx*, unsigned int> > setStakeCoins;
    static int nLastStakeSetUpdate = 0;

    if (GetTime() - nLastStakeSetUpdate > nStakeSetUpdateTime) {
        setStakeCoins.clear();
        if (!SelectStakeCoins(setStakeCoins, nBalance - nReserveBalance))
            return false;

        nLastStakeSetUpdate = GetTime();
    }

    if (setStakeCoins.empty())
        return false;

    vector<const CWalletTx*> vwtxPrev;

    CAmount nCredit = 0;
    CScript scriptPubKeyKernel;

    //prevent staking a time that won't be accepted
    if (GetAdjustedTime() <= chainActive.Tip()->nTime)
        MilliSleep(10000);

    BOOST_FOREACH (PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setStakeCoins) {
        //make sure that enough time has elapsed between
        CBlockIndex* pindex = NULL;
        BlockMap::iterator it = mapBlockIndex.find(pcoin.first->hashBlock);
        if (it != mapBlockIndex.end())
            pindex = it->second;
        else {
            if (fDebug)
                LogPrintf("CreateCoinStake() failed to find block index \n");
            continue;
        }

        // Read block header
        CBlockHeader block = pindex->GetBlockHeader();

        bool fKernelFound = false;
        uint256 hashProofOfStake = 0;
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        nTxNewTime = GetAdjustedTime();

        //iterates each utxo inside of CheckStakeKernelHash()
        if (CheckStakeKernelHash(nBits, block, *pcoin.first, prevoutStake, nTxNewTime, nHashDrift, false, hashProofOfStake, true)) {
            //Double check that this will pass time requirements
            if (nTxNewTime <= chainActive.Tip()->GetMedianTimePast()) {
                LogPrintf("CreateCoinStake() : kernel found, but it is too far in the past \n");
                continue;
            }

            // Found a kernel
            if (fDebug && GetBoolArg("-printcoinstake", false))
                LogPrintf("CreateCoinStake : kernel found\n");

            vector<valtype> vSolutions;
            txnouttype whichType;
            CScript scriptPubKeyOut;
            scriptPubKeyKernel = pcoin.first->vout[pcoin.second].scriptPubKey;
            if (!Solver(scriptPubKeyKernel, whichType, vSolutions)) {
                LogPrintf("CreateCoinStake : failed to parse kernel\n");
                break;
            }
            if (fDebug && GetBoolArg("-printcoinstake", false))
                LogPrintf("CreateCoinStake : parsed kernel type=%d\n", whichType);
            if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH) {
                if (fDebug && GetBoolArg("-printcoinstake", false))
                    LogPrintf("CreateCoinStake : no support for kernel type=%d\n", whichType);
                break; // only support pay to public key and pay to address
            }
            if (whichType == TX_PUBKEYHASH) // pay to address type
            {
                //convert to pay to public key type
                CKey key;
                if (!keystore.GetKey(uint160(vSolutions[0]), key)) {
                    if (fDebug && GetBoolArg("-printcoinstake", false))
                        LogPrintf("CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                    break; // unable to find corresponding public key
                }

                scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
            } else
                scriptPubKeyOut = scriptPubKeyKernel;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
            txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

            //presstab HyperStake - calculate the total size of our new output including the stake reward so that we can use it to decide whether to split the stake outputs
            const CBlockIndex* pIndex0 = chainActive.Tip();
            uint64_t nTotalSize = pcoin.first->vout[pcoin.second].nValue + GetBlockValue(pIndex0->nHeight);

            //presstab HyperStake - if MultiSend is set to send in coinstake we will add our outputs here (values asigned further down)
            if (nTotalSize / 2 > nStakeSplitThreshold * COIN)
                txNew.vout.push_back(CTxOut(0, scriptPubKeyOut)); //split stake

            if (fDebug && GetBoolArg("-printcoinstake", false))
                LogPrintf("CreateCoinStake : added kernel type=%d\n", whichType);
            fKernelFound = true;
            break;
        }
        if (fKernelFound)
            break; // if kernel is found stop searching
    }
    if (nCredit == 0 || nCredit > nBalance - nReserveBalance)
        return false;

    // Calculate reward
    CAmount nReward;
    const CBlockIndex* pIndex0 = chainActive.Tip();
    nReward = GetBlockValue(pIndex0->nHeight);
    nCredit += nReward;

    // Set output amount
    if (txNew.vout.size() == 3) {
        txNew.vout[1].nValue = nCredit / 2;
        txNew.vout[2].nValue = nCredit - txNew.vout[1].nValue;
    } else {
        txNew.vout[1].nValue = nCredit;
    }


#if 0
    //Masternode payment
	mnPayments.FillBlockPayee(txNew, nMinFee, true);
#endif

    // Sign
    int nIn = 0;
    BOOST_FOREACH (const CWalletTx* pcoin, vwtxPrev) {
        if (!SignSignature(*this, *pcoin, txNew, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Successfully generated coinstake
    nLastStakeSetUpdate = 0; //this will trigger stake set to repopulate next round
    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, std::string strCommand)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile, "r") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Notify that old coins are spent
            if (!wtxNew.IsZerocoinSpend()) {
                set<uint256> updated_hahes;
                BOOST_FOREACH (const CTxIn& txin, wtxNew.vin) {
                    // notify only once
                    if (updated_hahes.find(txin.prevout.hash) != updated_hahes.end()) continue;

                    CWalletTx& coin = mapWallet[txin.prevout.hash];
                    coin.BindWallet(this);
                    NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
                    updated_hahes.insert(txin.prevout.hash);
                }
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        // Broadcast
        if (!wtxNew.AcceptToMemoryPool(false)) {
            // This must not fail. The transaction has already been signed and recorded.
            LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
            return false;
        }
        wtxNew.RelayWalletTransaction(strCommand);
    }
    return true;
}

CAmount CWallet::GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool)
{
    // payTxFee is user-set "I want to pay this much"
    CAmount nFeeNeeded = payTxFee.GetFee(nTxBytes);
    // user selected total at least (default=true)
    if (fPayAtLeastCustomFee && nFeeNeeded > 0 && nFeeNeeded < payTxFee.GetFeePerK())
        nFeeNeeded = payTxFee.GetFeePerK();
    // User didn't set: use -txconfirmtarget to estimate...
    if (nFeeNeeded == 0)
        nFeeNeeded = pool.estimateFee(nConfirmTarget).GetFee(nTxBytes);
    // ... unless we don't have enough mempool data, in which case fall
    // back to a hard-coded fee
    if (nFeeNeeded == 0)
        nFeeNeeded = minTxFee.GetFee(nTxBytes);
    // prevent user from paying a non-sense fee (like 1 satoshi): 0 < fee < minRelayFee
    if (nFeeNeeded < ::minRelayTxFee.GetFee(nTxBytes))
        nFeeNeeded = ::minRelayTxFee.GetFee(nTxBytes);
    // But always obey the maximum
    if (nFeeNeeded > maxTxFee)
        nFeeNeeded = maxTxFee;
    return nFeeNeeded;
}

CAmount CWallet::GetTotalValue(std::vector<CTxIn> vCoins)
{
    CAmount nTotalValue = 0;
    CWalletTx wtx;
    BOOST_FOREACH (CTxIn i, vCoins) {
        if (mapWallet.count(i.prevout.hash)) {
            CWalletTx& wtx = mapWallet[i.prevout.hash];
            if (i.prevout.n < wtx.vout.size()) {
                nTotalValue += wtx.vout[i.prevout.n].nValue;
            }
        } else {
            LogPrintf("GetTotalValue -- Couldn't find transaction\n");
        }
    }
    return nTotalValue;
}

string CWallet::PrepareObfuscationDenominate(int minRounds, int maxRounds)
{
    if (IsLocked())
        return _("Error: Wallet locked, unable to create transaction!");

    if (obfuScationPool.GetState() != POOL_STATUS_ERROR && obfuScationPool.GetState() != POOL_STATUS_SUCCESS)
        if (obfuScationPool.GetEntriesCount() > 0)
            return _("Error: You already have pending entries in the Obfuscation pool");

    // ** find the coins we'll use
    std::vector<CTxIn> vCoins;
    std::vector<CTxIn> vCoinsResult;
    std::vector<COutput> vCoins2;
    CAmount nValueIn = 0;
    CReserveKey reservekey(this);

    /*
        Select the coins we'll use

        if minRounds >= 0 it means only denominated inputs are going in and coming out
    */
    if (minRounds >= 0) {
        if (!SelectCoinsByDenominations(obfuScationPool.sessionDenom, 0.1 * COIN, OBFUSCATION_POOL_MAX, vCoins, vCoins2, nValueIn, minRounds, maxRounds))
            return _("Error: Can't select current denominated inputs");
    }

    LogPrintf("PrepareObfuscationDenominate - preparing obfuscation denominate . Got: %d \n", nValueIn);

    {
        LOCK(cs_wallet);
        BOOST_FOREACH (CTxIn v, vCoins)
            LockCoin(v.prevout);
    }

    CAmount nValueLeft = nValueIn;
    std::vector<CTxOut> vOut;

    /*
        TODO: Front load with needed denominations (e.g. .1, 1 )
    */

    // Make outputs by looping through denominations: try to add every needed denomination, repeat up to 5-10 times.
    // This way we can be pretty sure that it should have at least one of each needed denomination.
    // NOTE: No need to randomize order of inputs because they were
    // initially shuffled in CWallet::SelectCoinsByDenominations already.
    int nStep = 0;
    int nStepsMax = 5 + GetRandInt(5);
    while (nStep < nStepsMax) {
        BOOST_FOREACH (CAmount v, obfuScationDenominations) {
            // only use the ones that are approved
            bool fAccepted = false;
            if ((obfuScationPool.sessionDenom & (1 << 0)) && v == ((10000 * COIN) + 10000000)) {
                fAccepted = true;
            } else if ((obfuScationPool.sessionDenom & (1 << 1)) && v == ((1000 * COIN) + 1000000)) {
                fAccepted = true;
            } else if ((obfuScationPool.sessionDenom & (1 << 2)) && v == ((100 * COIN) + 100000)) {
                fAccepted = true;
            } else if ((obfuScationPool.sessionDenom & (1 << 3)) && v == ((10 * COIN) + 10000)) {
                fAccepted = true;
            } else if ((obfuScationPool.sessionDenom & (1 << 4)) && v == ((1 * COIN) + 1000)) {
                fAccepted = true;
            } else if ((obfuScationPool.sessionDenom & (1 << 5)) && v == ((.1 * COIN) + 100)) {
                fAccepted = true;
            }
            if (!fAccepted) continue;

            // try to add it
            if (nValueLeft - v >= 0) {
                // Note: this relies on a fact that both vectors MUST have same size
                std::vector<CTxIn>::iterator it = vCoins.begin();
                std::vector<COutput>::iterator it2 = vCoins2.begin();
                while (it2 != vCoins2.end()) {
                    // we have matching inputs
                    if ((*it2).tx->vout[(*it2).i].nValue == v) {
                        // add new input in resulting vector
                        vCoinsResult.push_back(*it);
                        // remove corresponting items from initial vectors
                        vCoins.erase(it);
                        vCoins2.erase(it2);

                        CScript scriptChange;
                        CPubKey vchPubKey;
                        // use a unique change address
                        assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
                        scriptChange = GetScriptForDestination(vchPubKey.GetID());
                        reservekey.KeepKey();

                        // add new output
                        CTxOut o(v, scriptChange);
                        vOut.push_back(o);

                        // subtract denomination amount
                        nValueLeft -= v;

                        break;
                    }
                    ++it;
                    ++it2;
                }
            }
        }

        nStep++;

        if (nValueLeft == 0) break;
    }

    {
        // unlock unused coins
        LOCK(cs_wallet);
        BOOST_FOREACH (CTxIn v, vCoins)
            UnlockCoin(v.prevout);
    }

    if (obfuScationPool.GetDenominations(vOut) != obfuScationPool.sessionDenom) {
        // unlock used coins on failure
        LOCK(cs_wallet);
        BOOST_FOREACH (CTxIn v, vCoinsResult)
            UnlockCoin(v.prevout);
        return "Error: can't make current denominated outputs";
    }

    // randomize the output order
    std::random_shuffle(vOut.begin(), vOut.end());

    // We also do not care about full amount as long as we have right denominations, just pass what we found
    obfuScationPool.SendObfuscationDenominate(vCoinsResult, vOut, nValueIn - nValueLeft);

    return "";
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile, "cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE) {
        if (CDB::Rewrite(strWalletFile, "\x04pool")) {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}


DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile, "cr+").ZapWalletTx(this, vWtx);
    if (nZapWalletTxRet == DB_NEED_REWRITE) {
        if (CDB::Rewrite(strWalletFile, "\x04pool")) {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}


bool CWallet::SetAddressBook(const CTxDestination& address, const string& strName, const string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO,
        strPurpose, (fUpdated ? CT_UPDATED : CT_NEW));
    if (!fFileBacked)
        return false;
    if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(CBitcoinAddress(address).ToString(), strPurpose))
        return false;
    return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        if (fFileBacked) {
            // Delete destdata tuples associated with address
            std::string strAddress = CBitcoinAddress(address).ToString();
            BOOST_FOREACH (const PAIRTYPE(string, string) & item, mapAddressBook[address].destdata) {
                CWalletDB(strWalletFile).EraseDestData(strAddress, item.first);
            }
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

    if (!fFileBacked)
        return false;
    CWalletDB(strWalletFile).ErasePurpose(CBitcoinAddress(address).ToString());
    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
}

bool CWallet::SetDefaultKey(const CPubKey& vchPubKey)
{
    if (fFileBacked) {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        BOOST_FOREACH (int64_t nIndex, setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 1000), (int64_t)0);
        for (int i = 0; i < nKeys; i++) {
            int64_t nIndex = i + 1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = max(GetArg("-keypool", 1000), (int64_t)0);

        while (setKeyPool.size() < (nTargetSize + 1)) {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool() : writing generated key failed");
            setKeyPool.insert(nEnd);
            LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
            double dProgress = 100.f * nEnd / (nTargetSize + 1);
            std::string strMsg = strprintf(_("Loading wallet... (%3.2f %%)"), dProgress);
            uiInterface.InitMessage(strMsg);
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if (setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool() : read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LogPrintf("keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked) {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1) {
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH (PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet) {
            CWalletTx* pcoin = &walletEntry.second;

            if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.first, i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set<set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set<set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    BOOST_FOREACH (PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet) {
        CWalletTx* pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0) {
            bool any_mine = false;
            // group all input addresses with each other
            BOOST_FOREACH (CTxIn txin, pcoin->vin) {
                CTxDestination address;
                if (!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if (!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine) {
                BOOST_FOREACH (CTxOut txout, pcoin->vout)
                    if (IsChange(txout)) {
                        CTxDestination txoutAddr;
                        if (!ExtractDestination(txout.scriptPubKey, txoutAddr))
                            continue;
                        grouping.insert(txoutAddr);
                    }
            }
            if (grouping.size() > 0) {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i])) {
                CTxDestination address;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set<set<CTxDestination>*> uniqueGroupings;        // a set of pointers to groups of addresses
    map<CTxDestination, set<CTxDestination>*> setmap; // map addresses to the unique group containing it
    BOOST_FOREACH (set<CTxDestination> grouping, groupings) {
        // make a set of all the groups hit by this new group
        set<set<CTxDestination>*> hits;
        map<CTxDestination, set<CTxDestination>*>::iterator it;
        BOOST_FOREACH (CTxDestination address, grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        BOOST_FOREACH (set<CTxDestination>* hit, hits) {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH (CTxDestination element, *merged)
            setmap[element] = merged;
    }

    set<set<CTxDestination> > ret;
    BOOST_FOREACH (set<CTxDestination>* uniqueGrouping, uniqueGroupings) {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

set<CTxDestination> CWallet::GetAccountAddresses(string strAccount) const
{
    LOCK(cs_wallet);
    set<CTxDestination> result;
    BOOST_FOREACH (const PAIRTYPE(CTxDestination, CAddressBookData) & item, mapAddressBook) {
        const CTxDestination& address = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1) {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH (const int64_t& id, setKeyPool) {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes() : read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
        setAddress.insert(keyID);
    }
}

bool CWallet::UpdatedTransaction(const uint256& hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end()) {
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
            return true;
        }
    }
    return false;
}

void CWallet::LockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

/** @} */ // end of Actions

class CAffectedKeysVisitor : public boost::static_visitor<void>
{
private:
    const CKeyStore& keystore;
    std::vector<CKeyID>& vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore& keystoreIn, std::vector<CKeyID>& vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    void Process(const CScript& script)
    {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            BOOST_FOREACH (const CTxDestination& dest, vDest)
                boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID& keyId)
    {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID& scriptId)
    {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination& none) {}
};

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t>& mapKeyBirth) const
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex* pindexMax = chainActive[std::max(0, chainActive.Height() - 144)]; // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    BOOST_FOREACH (const CKeyID& keyid, setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx& wtx = (*it).second;
        BlockMap::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second)) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                BOOST_FOREACH (const CKeyID& keyid, vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->GetBlockTime() - 7200; // block times can be 2h off
}

bool CWallet::AddDestData(const CTxDestination& dest, const std::string& key, const std::string& value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteDestData(CBitcoinAddress(dest).ToString(), key, value);
}

bool CWallet::EraseDestData(const CTxDestination& dest, const std::string& key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).EraseDestData(CBitcoinAddress(dest).ToString(), key);
}

bool CWallet::LoadDestData(const CTxDestination& dest, const std::string& key, const std::string& value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return true;
}

bool CWallet::GetDestData(const CTxDestination& dest, const std::string& key, std::string* value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if (i != mapAddressBook.end()) {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if (j != i->second.destdata.end()) {
            if (value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

// CWallet::AutoZeromint() gets called with each new incoming block
void CWallet::AutoZeromint()
{
    // Don't bother Autominting if Zerocoin Protocol isn't active
    if (GetAdjustedTime() > GetSporkValue(SPORK_16_ZEROCOIN_MAINTENANCE_MODE)) return;

    // Wait until blockchain + masternodes are fully synced and wallet is unlocked.
    if (!masternodeSync.IsSynced() || IsLocked()){
        // Re-adjust startup time in case syncing needs a long time.
        nStartupTime = GetAdjustedTime();
        return;
    }

    // After sync wait even more to reduce load when wallet was just started
    int64_t nWaitTime = GetAdjustedTime() - nStartupTime;
    if (nWaitTime < AUTOMINT_DELAY){
        LogPrint("zero", "CWallet::AutoZeromint(): time since sync-completion or last Automint (%ld sec) < default waiting time (%ld sec). Waiting again...\n", nWaitTime, AUTOMINT_DELAY);
        return;
    }

    CAmount nZerocoinBalance = GetZerocoinBalance(false); //false includes both pending and mature zerocoins. Need total balance for this so nothing is overminted.
    CAmount nBalance = GetUnlockedCoins(); // We only consider unlocked coins, this also excludes masternode-vins
                                           // from being accidentally minted
    CAmount nMintAmount = 0;
    CAmount nToMintAmount = 0;

    // zDIV are integers > 0, so we can't mint 10% of 9 DIV
    if (nBalance < 10){
        LogPrint("zero", "CWallet::AutoZeromint(): available balance (%ld) too small for minting zDIV\n", nBalance);
        return;
    }

    // Percentage of zDIV we already have
    double dPercentage = 100 * (double)nZerocoinBalance / (double)(nZerocoinBalance + nBalance);

    // Check if minting is actually needed
    if(dPercentage >= nZeromintPercentage){
        LogPrint("zero", "CWallet::AutoZeromint() @block %ld: percentage of existing zDIV (%lf%%) already >= configured percentage (%d%%). No minting needed...\n",
                  chainActive.Tip()->nHeight, dPercentage, nZeromintPercentage);
        return;
    }

    // zDIV amount needed for the target percentage
    nToMintAmount = ((nZerocoinBalance + nBalance) * nZeromintPercentage / 100);

    // zDIV amount missing from target (must be minted)
    nToMintAmount = (nToMintAmount - nZerocoinBalance) / COIN;

    // Use the biggest denomination smaller than the needed zDIV We'll only mint exact denomination to make minting faster.
    // Exception: for big amounts use 6666 (6666 = 1*5000 + 1*1000 + 1*500 + 1*100 + 1*50 + 1*10 + 1*5 + 1) to create all
    // possible denominations to avoid having 5000 denominations only.
    // If a preferred denomination is used (means nPreferredDenom != 0) do nothing until we have enough DIV to mint this denomination

    if (nPreferredDenom > 0){
        if (nToMintAmount >= nPreferredDenom)
            nToMintAmount = nPreferredDenom;  // Enough coins => mint preferred denomination
        else
            nToMintAmount = 0;                // Not enough coins => do nothing and wait for more coins
    }

    if (nToMintAmount >= ZQ_6666){
        nMintAmount = ZQ_6666;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_FIVE_THOUSAND){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_FIVE_THOUSAND;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_FIVE_HUNDRED){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_FIVE_HUNDRED;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_FIFTY){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_FIFTY;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_TEN){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_TEN;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_FIVE){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_FIVE;
    } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_ONE){
        nMintAmount = libzerocoin::CoinDenomination::ZQ_ONE;
    } else {
        nMintAmount = 0;
    }

    if (nMintAmount > 0){
    CWalletTx wtx;
        vector<CZerocoinMint> vMints;
        string strError = pwalletMain->MintZerocoin(nMintAmount*COIN, wtx, vMints);

        // Return if something went wrong during minting
        if (strError != ""){
            LogPrintf("CWallet::AutoZeromint(): auto minting failed with error: %s\n", strError);
            return;
        }
        nZerocoinBalance = GetZerocoinBalance(false);
        nBalance = GetUnlockedCoins();
        dPercentage = 100 * (double)nZerocoinBalance / (double)(nZerocoinBalance + nBalance);
        LogPrintf("CWallet::AutoZeromint() @ block %ld: successfully minted %ld zDIV. Current percentage of zDIV: %lf%%\n",
                  chainActive.Tip()->nHeight, nMintAmount, dPercentage);
        // Re-adjust startup time to delay next Automint for 5 minutes
        nStartupTime = GetAdjustedTime();
    }
    else {
        LogPrintf("CWallet::AutoZeromint(): Nothing minted because either not enough funds available or the requested denomination size (%d) is not yet reached.\n", nPreferredDenom);
    }
}

void CWallet::AutoCombineDust()
{
    if (IsInitialBlockDownload() || IsLocked()) {
        return;
    }

    map<CBitcoinAddress, vector<COutput> > mapCoinsByAddress = AvailableCoinsByAddress(true, 0);

    //coins are sectioned by address. This combination code only wants to combine inputs that belong to the same address
    for (map<CBitcoinAddress, vector<COutput> >::iterator it = mapCoinsByAddress.begin(); it != mapCoinsByAddress.end(); it++) {
        vector<COutput> vCoins, vRewardCoins;
        vCoins = it->second;

        //find masternode rewards that need to be combined
        CCoinControl* coinControl = new CCoinControl();
        CAmount nTotalRewardsValue = 0;
        BOOST_FOREACH (const COutput& out, vCoins) {
            //no coins should get this far if they dont have proper maturity, this is double checking
            if (out.tx->IsCoinStake() && out.tx->GetDepthInMainChain() < COINBASE_MATURITY + 1)
                continue;

            if (out.Value() > nAutoCombineThreshold * COIN)
                continue;

            COutPoint outpt(out.tx->GetHash(), out.i);
            coinControl->Select(outpt);
            vRewardCoins.push_back(out);
            nTotalRewardsValue += out.Value();
        }

        //if no inputs found then return
        if (!coinControl->HasSelected())
            continue;

        //we cannot combine one coin with itself
        if (vRewardCoins.size() <= 1)
            continue;

        vector<pair<CScript, CAmount> > vecSend;
        CScript scriptPubKey = GetScriptForDestination(it->first.Get());
        vecSend.push_back(make_pair(scriptPubKey, nTotalRewardsValue));

        // Create the transaction and commit it to the network
        CWalletTx wtx;
        CReserveKey keyChange(this); // this change address does not end up being used, because change is returned with coin control switch
        string strErr;
        CAmount nFeeRet = 0;

        //get the fee amount
        CWalletTx wtxdummy;
        CreateTransaction(vecSend, wtxdummy, keyChange, nFeeRet, strErr, coinControl, ALL_COINS, false, CAmount(0));
        vecSend[0].second = nTotalRewardsValue - nFeeRet - 500;

        if (!CreateTransaction(vecSend, wtx, keyChange, nFeeRet, strErr, coinControl, ALL_COINS, false, CAmount(0))) {
            LogPrintf("AutoCombineDust createtransaction failed, reason: %s\n", strErr);
            continue;
        }

        if (!CommitTransaction(wtx, keyChange)) {
            LogPrintf("AutoCombineDust transaction commit failed\n");
            continue;
        }

        LogPrintf("AutoCombineDust sent transaction\n");

        delete coinControl;
    }
}

bool CWallet::MultiSend()
{
    if (IsInitialBlockDownload() || IsLocked()) {
        return false;
    }

    if (chainActive.Tip()->nHeight <= nLastMultiSendHeight) {
        LogPrintf("Multisend: lastmultisendheight is higher than current best height\n");
        return false;
    }

    std::vector<COutput> vCoins;
    AvailableCoins(vCoins);
    int stakeSent = 0;
    int mnSent = 0;
    BOOST_FOREACH (const COutput& out, vCoins) {
        //need output with precise confirm count - this is how we identify which is the output to send
        if (out.tx->GetDepthInMainChain() != COINBASE_MATURITY + 1)
            continue;

        COutPoint outpoint(out.tx->GetHash(), out.i);
        bool sendMSonMNReward = fMultiSendMasternodeReward && outpoint.IsMasternodeReward(out.tx);
        bool sendMSOnStake = fMultiSendStake && out.tx->IsCoinStake() && !sendMSonMNReward; //output is either mnreward or stake reward, not both

        if (!(sendMSOnStake || sendMSonMNReward))
            continue;

        CTxDestination destMyAddress;
        if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, destMyAddress)) {
            LogPrintf("Multisend: failed to extract destination\n");
            continue;
        }

        //Disabled Addresses won't send MultiSend transactions
        if (vDisabledAddresses.size() > 0) {
            for (unsigned int i = 0; i < vDisabledAddresses.size(); i++) {
                if (vDisabledAddresses[i] == CBitcoinAddress(destMyAddress).ToString()) {
                    LogPrintf("Multisend: disabled address preventing multisend\n");
                    return false;
                }
            }
        }

        // create new coin control, populate it with the selected utxo, create sending vector
        CCoinControl* cControl = new CCoinControl();
        COutPoint outpt(out.tx->GetHash(), out.i);
        cControl->Select(outpt);
        cControl->destChange = destMyAddress;

        CWalletTx wtx;
        CReserveKey keyChange(this); // this change address does not end up being used, because change is returned with coin control switch
        CAmount nFeeRet = 0;
        vector<pair<CScript, CAmount> > vecSend;

        // loop through multisend vector and add amounts and addresses to the sending vector
        const isminefilter filter = ISMINE_SPENDABLE;
        CAmount nAmount = 0;
        for (unsigned int i = 0; i < vMultiSend.size(); i++) {
            // MultiSend vector is a pair of 1)Address as a std::string 2) Percent of stake to send as an int
            nAmount = ((out.tx->GetCredit(filter) - out.tx->GetDebit(filter)) * vMultiSend[i].second) / 100;
            CBitcoinAddress strAddSend(vMultiSend[i].first);
            CScript scriptPubKey;
            scriptPubKey = GetScriptForDestination(strAddSend.Get());
            vecSend.push_back(make_pair(scriptPubKey, nAmount));
        }

        //get the fee amount
        CWalletTx wtxdummy;
        string strErr;
        CreateTransaction(vecSend, wtxdummy, keyChange, nFeeRet, strErr, cControl, ALL_COINS, false, CAmount(0));
        CAmount nLastSendAmount = vecSend[vecSend.size() - 1].second;
        if (nLastSendAmount < nFeeRet + 500) {
            LogPrintf("%s: fee of %d is too large to insert into last output\n", __func__, nFeeRet + 500);
            return false;
        }
        vecSend[vecSend.size() - 1].second = nLastSendAmount - nFeeRet - 500;

        // Create the transaction and commit it to the network
        if (!CreateTransaction(vecSend, wtx, keyChange, nFeeRet, strErr, cControl, ALL_COINS, false, CAmount(0))) {
            LogPrintf("MultiSend createtransaction failed\n");
            return false;
        }

        if (!CommitTransaction(wtx, keyChange)) {
            LogPrintf("MultiSend transaction commit failed\n");
            return false;
        } else
            fMultiSendNotify = true;

        delete cControl;

        //write nLastMultiSendHeight to DB
        CWalletDB walletdb(strWalletFile);
        nLastMultiSendHeight = chainActive.Tip()->nHeight;
        if (!walletdb.WriteMSettings(fMultiSendStake, fMultiSendMasternodeReward, nLastMultiSendHeight))
            LogPrintf("Failed to write MultiSend setting to DB\n");

        LogPrintf("MultiSend successfully sent\n");
        if (sendMSOnStake)
            stakeSent++;
        else
            mnSent++;

        //stop iterating if we are done
        if (mnSent > 0 && stakeSent > 0)
            return true;
        if (stakeSent > 0 && !fMultiSendMasternodeReward)
            return true;
        if (mnSent > 0 && !fMultiSendStake)
            return true;
    }

    return true;
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    AssertLockHeld(cs_main);
    CBlock blockTmp;

    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)block.vtx.size()) {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return 0;
    }

    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    const CBlockIndex* pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChainINTERNAL(const CBlockIndex*& pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified) {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(const CBlockIndex*& pindexRet, bool enableIX) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    if (enableIX) {
        if (nResult < 6) {
            int signatures = GetTransactionLockSignatures();
            if (signatures >= SWIFTTX_SIGNATURES_REQUIRED) {
                return nSwiftTXDepth + nResult;
            }
        }
    }

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return max(0, (Params().COINBASE_MATURITY() + 1) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectInsaneFee, bool ignoreFees)
{
    CValidationState state;
    bool fAccepted = ::AcceptToMemoryPool(mempool, state, *this, fLimitFree, NULL, fRejectInsaneFee, ignoreFees);
    if (!fAccepted)
        LogPrintf("%s : %s\n", __func__, state.GetRejectReason());
    return fAccepted;
}

int CMerkleTx::GetTransactionLockSignatures() const
{
    if (fLargeWorkForkFound || fLargeWorkInvalidChainFound) return -2;
    if (!IsSporkActive(SPORK_2_SWIFTTX)) return -3;
    if (!fEnableSwiftTX) return -1;

    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());
    if (i != mapTxLocks.end()) {
        return (*i).second.CountSignatures();
    }

    return -1;
}

bool CMerkleTx::IsTransactionLockTimedOut() const
{
    if (!fEnableSwiftTX) return 0;

    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());
    if (i != mapTxLocks.end()) {
        return GetTime() > (*i).second.nTimeout;
    }

    return false;
}

bool CWallet::CreateZerocoinMintTransaction(const CAmount nValue, CMutableTransaction& txNew, vector<CZerocoinMint>& vMints, CReserveKey* reservekey, int64_t& nFeeRet, std::string& strFailReason, const CCoinControl* coinControl, const bool isZCSpendChange)
{
    if (IsLocked()) {
        strFailReason = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("SpendZerocoin() : %s", strFailReason.c_str());
        return false;
    }

    //add multiple mints that will fit the amount requested as closely as possible
    CAmount nMintingValue = 0;
    CAmount nValueRemaining = 0;
    while (true) {
        //mint a coin with the closest denomination to what is being requested
        nFeeRet = max(static_cast<int>(txNew.vout.size()), 1) * Params().Zerocoin_MintFee();
        nValueRemaining = nValue - nMintingValue - (isZCSpendChange ? nFeeRet : 0);

        // if this is change of a zerocoinspend, then we can't mint all change, at least something must be given as a fee
        if (isZCSpendChange && nValueRemaining <= 1 * COIN)
            break;

        libzerocoin::CoinDenomination denomination = libzerocoin::AmountToClosestDenomination(nValueRemaining, nValueRemaining);
        if (denomination == libzerocoin::ZQ_ERROR)
            break;

        CAmount nValueNewMint = libzerocoin::ZerocoinDenominationToAmount(denomination);
        nMintingValue += nValueNewMint;

        // mint a new coin (create Pedersen Commitment) and extract PublicCoin that is shareable from it
        libzerocoin::PrivateCoin newCoin(Params().Zerocoin_Params(), denomination);
        libzerocoin::PublicCoin pubCoin = newCoin.getPublicCoin();

        // Validate
        if(!pubCoin.validate()) {
            strFailReason = _("failed to validate zerocoin");
            return false;
        }

        CScript scriptSerializedCoin = CScript() << OP_ZEROCOINMINT << pubCoin.getValue().getvch().size() << pubCoin.getValue().getvch();
        CTxOut outMint(nValueNewMint, scriptSerializedCoin);
        txNew.vout.push_back(outMint);

        //store as CZerocoinMint for later use
        CZerocoinMint mint(denomination, pubCoin.getValue(), newCoin.getRandomness(), newCoin.getSerialNumber(), false);
        vMints.push_back(mint);
    }

    // calculate fee
    CAmount nFee = Params().Zerocoin_MintFee() * txNew.vout.size();

    // no ability to select more coins if this is a ZCSpend change mint
    CAmount nTotalValue = (isZCSpendChange ? nValue : (nValue + nFee));

    // check for a zerocoinspend that mints the change
    CAmount nValueIn = 0;
    set<pair<const CWalletTx*, unsigned int> > setCoins;
    if (isZCSpendChange) {
        nValueIn = nValue;
    } else {
        // select UTXO's to use
        if (!SelectCoins(nTotalValue, setCoins, nValueIn, coinControl)) {
            strFailReason = _("Insufficient or insufficient confirmed funds, you might need to wait a few minutes and try again.");
            return false;
        }

        // Fill vin
        for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins)
            txNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));
    }

    //any change that is less than 0.0100000 will be ignored and given as an extra fee
    //also assume that a zerocoinspend that is minting the change will not have any change that goes to Div
    CAmount nChange = nValueIn - nTotalValue; // Fee already accounted for in nTotalValue
    if (nChange > 1 * CENT && !isZCSpendChange) {
        // Fill a vout to ourself
        CScript scriptChange;

        // if coin control: send change to custom address
        if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
            scriptChange = GetScriptForDestination(coinControl->destChange);
        else {
            // Reserve a new key pair from key pool
            CPubKey vchPubKey;
            assert(reservekey->GetReservedKey(vchPubKey)); // should never fail, as we just unlocked
            scriptChange = GetScriptForDestination(vchPubKey.GetID());
        }

        //add to the transaction
        CTxOut outChange(nChange, scriptChange);
        txNew.vout.push_back(outChange);
    } else {
        if (reservekey)
            reservekey->ReturnKey();
    }

    // Sign if these are divi outputs - NOTE that zDiv outputs are signed later in SoK
    if (!isZCSpendChange) {
        int nIn = 0;
        for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins) {
            if (!SignSignature(*this, *coin.first, txNew, nIn++)) {
                strFailReason = _("Signing transaction failed");
                return false;
            }
        }
    }

    return true;
}

bool CWallet::MintToTxIn(CZerocoinMint zerocoinSelected, int nSecurityLevel, const uint256& hashTxOut, CTxIn& newTxIn, CZerocoinSpendReceipt& receipt)
{
    // Default error status if not changed below
    receipt.SetStatus(_("Transaction Mint Started"), ZDIV_TXMINT_GENERAL);

    libzerocoin::CoinDenomination denomination = zerocoinSelected.GetDenomination();
    // 2. Get pubcoin from the private coin
    libzerocoin::PublicCoin pubCoinSelected(Params().Zerocoin_Params(), zerocoinSelected.GetValue(), denomination);
    LogPrintf("%s : pubCoinSelected:\n denom=%d\n value%s\n", __func__, denomination, pubCoinSelected.getValue().GetHex());
    if (!pubCoinSelected.validate()) {
        receipt.SetStatus(_("The selected mint coin is an invalid coin"), ZDIV_INVALID_COIN);
        return false;
    }

    // 3. Compute Accumulator and Witness
    libzerocoin::Accumulator accumulator(Params().Zerocoin_Params(), pubCoinSelected.getDenomination());
    libzerocoin::AccumulatorWitness witness(Params().Zerocoin_Params(), accumulator, pubCoinSelected);
    string strFailReason = "";
    int nMintsAdded = 0;
    if (!GenerateAccumulatorWitness(pubCoinSelected, accumulator, witness, nSecurityLevel, nMintsAdded, strFailReason)) {
        receipt.SetStatus(_("Try to spend with a higher security level to include more coins"), ZDIV_FAILED_ACCUMULATOR_INITIALIZATION);
        LogPrintf("%s : %s \n", __func__, receipt.GetStatusMessage());
        return false;
    }

    // Construct the CoinSpend object. This acts like a signature on the transaction.
    libzerocoin::PrivateCoin privateCoin(Params().Zerocoin_Params(), denomination);
    privateCoin.setPublicCoin(pubCoinSelected);
    privateCoin.setRandomness(zerocoinSelected.GetRandomness());
    privateCoin.setSerialNumber(zerocoinSelected.GetSerialNumber());
    uint32_t nChecksum = GetChecksum(accumulator.getValue());

    try {
        libzerocoin::CoinSpend spend(Params().Zerocoin_Params(), privateCoin, accumulator, nChecksum, witness, hashTxOut);

        if (!spend.Verify(accumulator)) {
            receipt.SetStatus(_("The new spend coin transaction did not verify"), ZDIV_INVALID_WITNESS);
            return false;
        }

        // Deserialize the CoinSpend intro a fresh object
        CDataStream serializedCoinSpend(SER_NETWORK, PROTOCOL_VERSION);
        serializedCoinSpend << spend;
        std::vector<unsigned char> data(serializedCoinSpend.begin(), serializedCoinSpend.end());

        //Add the coin spend into a DIVI transaction
        newTxIn.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
        newTxIn.scriptSig.insert(newTxIn.scriptSig.end(), data.begin(), data.end());
        newTxIn.prevout.SetNull();

        //use nSequence as a shorthand lookup of denomination
        //NOTE that this should never be used in place of checking the value in the final blockchain acceptance/verification
        //of the transaction
        newTxIn.nSequence = denomination;

        CDataStream serializedCoinSpendChecking(SER_NETWORK, PROTOCOL_VERSION);
        try {
            serializedCoinSpendChecking << spend;
        }
        catch (...) {
            receipt.SetStatus(_("Failed to deserialize"), ZDIV_BAD_SERIALIZATION);
            return false;
        }

        libzerocoin::CoinSpend newSpendChecking(Params().Zerocoin_Params(), serializedCoinSpendChecking);
        if (!newSpendChecking.Verify(accumulator)) {
            receipt.SetStatus(_("The transaction did not verify"), ZDIV_BAD_SERIALIZATION);
            return false;
        }

        std::list<CBigNum> listCoinSpendSerial = CWalletDB(strWalletFile).ListSpentCoinsSerial();
        for (const CBigNum& item : listCoinSpendSerial) {
            if (spend.getCoinSerialNumber() == item) {
                //Tried to spend an already spent zDiv
                zerocoinSelected.SetUsed(true);
                if (!CWalletDB(strWalletFile).WriteZerocoinMint(zerocoinSelected))
                    LogPrintf("%s failed to write zerocoinmint\n", __func__);

                pwalletMain->NotifyZerocoinChanged(pwalletMain, zerocoinSelected.GetValue().GetHex(), "Used", CT_UPDATED);
                receipt.SetStatus(_("The coin spend has been used"), ZDIV_SPENT_USED_ZDIV);
                return false;
            }
        }

        uint32_t nAccumulatorChecksum = GetChecksum(accumulator.getValue());
        CZerocoinSpend zcSpend(spend.getCoinSerialNumber(), 0, zerocoinSelected.GetValue(), zerocoinSelected.GetDenomination(), nAccumulatorChecksum);
        zcSpend.SetMintCount(nMintsAdded);
        receipt.AddSpend(zcSpend);
    }
    catch (const std::exception&) {
        receipt.SetStatus(_("CoinSpend: Accumulator witness does not verify"), ZDIV_INVALID_WITNESS);
        return false;
    }

    receipt.SetStatus(_("Spend Valid"), ZDIV_SPEND_OKAY); // Everything okay

    return true;
}

bool CWallet::CreateZerocoinSpendTransaction(CAmount nValue, int nSecurityLevel, CWalletTx& wtxNew, CReserveKey& reserveKey, CZerocoinSpendReceipt& receipt, vector<CZerocoinMint>& vSelectedMints, vector<CZerocoinMint>& vNewMints, bool fMintChange,  bool fMinimizeChange, CBitcoinAddress* address)
{
    // Check available funds
    int nStatus = ZDIV_TRX_FUNDS_PROBLEMS;
    if (nValue > GetZerocoinBalance(true)) {
        receipt.SetStatus(_("You don't have enough Zerocoins in your wallet"), nStatus);
        return false;
    }

    if (nValue < 1) {
        receipt.SetStatus(_("Value is below the the smallest available denomination (= 1) of zDiv"), nStatus);
        return false;
    }

    // Create transaction
    nStatus = ZDIV_TRX_CREATE;

    // If not already given pre-selected mints, then select mints from the wallet
    CWalletDB walletdb(pwalletMain->strWalletFile);
    list<CZerocoinMint> listMints;
    CAmount nValueSelected = 0;
    int nCoinsReturned = 0; // Number of coins returned in change from function below (for debug)
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed
    const int nMaxSpends = Params().Zerocoin_MaxSpendsPerTransaction(); // Maximum possible spends for one zDIV transaction
    if (vSelectedMints.empty()) {
        listMints = walletdb.ListMintedCoins(true, true, true); // need to find mints to spend
        if(listMints.empty()) {
            receipt.SetStatus(_("Failed to find Zerocoins in in wallet.dat"), nStatus);
            return false;
        }

        // If the input value is not an int, then we want the selection algorithm to round up to the next highest int
        double dValue = static_cast<double>(nValue) / static_cast<double>(COIN);
        bool fWholeNumber = floor(dValue) == dValue;
        CAmount nValueToSelect = nValue;
        if(!fWholeNumber)
            nValueToSelect = static_cast<CAmount>(ceil(dValue) * COIN);

        // Select the zDiv mints to use in this spend
        std::map<libzerocoin::CoinDenomination, CAmount> DenomMap = GetMyZerocoinDistribution();
        vSelectedMints = SelectMintsFromList(nValueToSelect, nValueSelected, nMaxSpends, fMinimizeChange,
                                             nCoinsReturned, listMints, DenomMap, nNeededSpends);
    } else {
        for (const CZerocoinMint mint : vSelectedMints)
            nValueSelected += ZerocoinDenominationToAmount(mint.GetDenomination());
    }

    listSpends(vSelectedMints);

    int nArchived = 0;
    for (CZerocoinMint mint : vSelectedMints) {
        // see if this serial has already been spent
        if (IsSerialKnown(mint.GetSerialNumber())) {
            receipt.SetStatus(_("Trying to spend an already spent serial #, try again."), nStatus);

            mint.SetUsed(true);
            walletdb.WriteZerocoinMint(mint);

            return false;
        }

        //check that this mint made it into the blockchain
        CTransaction txMint;
        uint256 hashBlock;
        bool fArchive = false;
        if (!GetTransaction(mint.GetTxHash(), txMint, hashBlock)) {
            receipt.SetStatus(_("Unable to find transaction containing mint"), nStatus);
            fArchive = true;
        } else if (mapBlockIndex.count(hashBlock) < 1) {
            receipt.SetStatus(_("Mint did not make it into blockchain"), nStatus);
            fArchive = true;
        }

        // archive this mint as an orphan
        if (fArchive) {
            walletdb.ArchiveMintOrphan(mint);
            nArchived++;
        }
    }
    if (nArchived)
        return false;

    if (vSelectedMints.empty()) {
        if(nNeededSpends > 0){
            // Too much spends needed, so abuse nStatus to report back the number of needed spends
            receipt.SetStatus(_("Too many spends needed"), nStatus, nNeededSpends);
        }
        else {
            receipt.SetStatus(_("Failed to select a zerocoin"), nStatus);
        }
        return false;
    }

    if ((static_cast<int>(vSelectedMints.size()) > Params().Zerocoin_MaxSpendsPerTransaction())) {
        receipt.SetStatus(_("Failed to find coin set amongst held coins with less than maxNumber of Spends"), nStatus);
        return false;
    }

    // Create change if needed
    nStatus = ZDIV_TRX_CHANGE;

    CMutableTransaction txNew;
    wtxNew.BindWallet(this);
    {
        LOCK2(cs_main, cs_wallet);
        {
            txNew.vin.clear();
            txNew.vout.clear();

            //if there is an address to send to then use it, if not generate a new address to send to
            CScript scriptZerocoinSpend;
            CScript scriptChange;
            CAmount nChange = nValueSelected - nValue;
            if (nChange && !address) {
                receipt.SetStatus(_("Need address because change is not exact"), nStatus);
                return false;
            }

            if (address) {
                scriptZerocoinSpend = GetScriptForDestination(address->Get());
                if (nChange) {
                    // Reserve a new key pair from key pool
                    CPubKey vchPubKey;
                    assert(reserveKey.GetReservedKey(vchPubKey)); // should never fail
                    scriptChange = GetScriptForDestination(vchPubKey.GetID());
                }
            } else {
                // Reserve a new key pair from key pool
                CPubKey vchPubKey;
                assert(reserveKey.GetReservedKey(vchPubKey)); // should never fail
                scriptZerocoinSpend = GetScriptForDestination(vchPubKey.GetID());
            }

            //add change output if we are spending too much (only applies to spending multiple at once)
            if (nChange) {
                //mint change as zerocoins
                if (fMintChange) {
                    CAmount nFeeRet = 0;
                    string strFailReason = "";
                    if (!CreateZerocoinMintTransaction(nChange, txNew, vNewMints, &reserveKey, nFeeRet, strFailReason, NULL, true)) {
                        receipt.SetStatus(_("Failed to create mint"), nStatus);
                        return false;
                    }
                } else {
                    CTxOut txOutChange(nValueSelected - nValue, scriptChange);
                    txNew.vout.push_back(txOutChange);
                }
            }

            //add output to divi address to the transaction (the actual primary spend taking place)
            CTxOut txOutZerocoinSpend(nValue, scriptZerocoinSpend);
            txNew.vout.push_back(txOutZerocoinSpend);

            //hash with only the output info in it to be used in Signature of Knowledge
            uint256 hashTxOut = txNew.GetHash();

            //add all of the mints to the transaction as inputs
            for (CZerocoinMint mint : vSelectedMints) {
                CTxIn newTxIn;
                if (!MintToTxIn(mint, nSecurityLevel, hashTxOut, newTxIn, receipt)) {
                    return false;
                }
                txNew.vin.push_back(newTxIn);
            }

            // Limit size
            unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
            if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
                receipt.SetStatus(_("In rare cases, a spend with 7 coins exceeds our maximum allowable transaction size, please retry spend using 6 or less coins"), ZDIV_TX_TOO_LARGE);
                return false;
            }

            //now that all inputs have been added, add full tx hash to zerocoinspend records and write to db
            uint256 txHash = txNew.GetHash();
            for (CZerocoinSpend spend : receipt.GetSpends()) {
                spend.SetTxHash(txHash);

                if (!CWalletDB(strWalletFile).WriteZerocoinSpendSerialEntry(spend)) {
                    receipt.SetStatus(_("Failed to write coin serial number into wallet"), nStatus);
                }
            }

            //turn the finalized transaction into a wallet transaction
            wtxNew = CWalletTx(this, txNew);
            wtxNew.fFromMe = true;
            wtxNew.fTimeReceivedIsTxTime = true;
            wtxNew.nTimeReceived = GetAdjustedTime();
        }
    }

    receipt.SetStatus(_("Transaction Created"), ZDIV_SPEND_OKAY); // Everything okay

    return true;
}

string CWallet::ResetMintZerocoin(bool fExtendedSearch)
{
    long updates = 0;
    long deletions = 0;
    CWalletDB walletdb(pwalletMain->strWalletFile);

    list<CZerocoinMint> listMints = walletdb.ListMintedCoins(false, false, true);
    vector<CZerocoinMint> vMintsToFind{ std::make_move_iterator(std::begin(listMints)), std::make_move_iterator(std::end(listMints)) };
    vector<CZerocoinMint> vMintsMissing;
    vector<CZerocoinMint> vMintsToUpdate;

    // search all of our available data for these mints
    FindMints(vMintsToFind, vMintsToUpdate, vMintsMissing, fExtendedSearch);

    // Update the meta data of mints that were marked for updating
    for (CZerocoinMint mint : vMintsToUpdate) {
        updates++;
        walletdb.WriteZerocoinMint(mint);
    }

    // Delete any mints that were unable to be located on the blockchain
    for (CZerocoinMint mint : vMintsMissing) {
        deletions++;
        walletdb.ArchiveMintOrphan(mint);
    }

    string strResult = _("ResetMintZerocoin finished: ") + to_string(updates) + _(" mints updated, ") + to_string(deletions) + _(" mints deleted\n");
    return strResult;
}

string CWallet::ResetSpentZerocoin()
{
    long removed = 0;
    CWalletDB walletdb(pwalletMain->strWalletFile);

    list<CZerocoinMint> listMints = walletdb.ListMintedCoins(false, false, false);
    list<CZerocoinSpend> listSpends = walletdb.ListSpentCoins();
    list<CZerocoinSpend> listUnconfirmedSpends;

    for (CZerocoinSpend spend : listSpends) {
        CTransaction tx;
        uint256 hashBlock = 0;
        if (!GetTransaction(spend.GetTxHash(), tx, hashBlock)) {
            listUnconfirmedSpends.push_back(spend);
            continue;
        }

        //no confirmations
        if (hashBlock == 0)
            listUnconfirmedSpends.push_back(spend);
    }

    for (CZerocoinSpend spend : listUnconfirmedSpends) {
        for (CZerocoinMint mint : listMints) {
            if (mint.GetSerialNumber() == spend.GetSerial()) {
                removed++;
                mint.SetUsed(false);
                RemoveSerialFromDB(spend.GetSerial());
                walletdb.WriteZerocoinMint(mint);
                walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial());
                continue;
            }
        }
    }

    string strResult = _("ResetSpentZerocoin finished: ") + to_string(removed) + _(" unconfirmed transactions removed\n");
    return strResult;
}

void CWallet::ReconsiderZerocoins(std::list<CZerocoinMint>& listMintsRestored)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    list<CZerocoinMint> listMints = walletdb.ListArchivedZerocoins();

    if (listMints.empty())
        return;

    for (CZerocoinMint mint : listMints) {
        if (IsSerialKnown(mint.GetSerialNumber()))
            continue;

        uint256 txHash;
        if (!GetZerocoinMint(mint.GetValue(), txHash))
            continue;

        uint256 hashBlock = 0;
        CTransaction tx;
        if (!GetTransaction(txHash, tx, hashBlock))
            continue;

        mint.SetTxHash(txHash);
        mint.SetHeight(mapBlockIndex.at(hashBlock)->nHeight);
        if (!walletdb.UnarchiveZerocoin(mint)) {
            LogPrintf("%s : failed to unarchive mint %s\n", __func__, mint.GetValue().GetHex());
        }
        listMintsRestored.emplace_back(mint);
    }
}


void CWallet::ZDivBackupWallet()
{
    filesystem::path backupDir = GetDataDir() / "backups";
    filesystem::path backupPath;
    string strNewBackupName;

    for (int i = 0; i < 10; i++) {
        strNewBackupName = strprintf("wallet-autozdivbackup-%d.dat", i);
        backupPath = backupDir / strNewBackupName;

        if (filesystem::exists(backupPath)) {
            //Keep up to 10 backups
            if (i <= 8) {
                //If the next file backup exists and is newer, then iterate
                filesystem::path nextBackupPath = backupDir / strprintf("wallet-autozdivbackup-%d.dat", i + 1);
                if (filesystem::exists(nextBackupPath)) {
                    time_t timeThis = filesystem::last_write_time(backupPath);
                    time_t timeNext = filesystem::last_write_time(nextBackupPath);
                    if (timeThis > timeNext) {
                        //The next backup is created before this backup was
                        //The next backup is the correct path to use
                        backupPath = nextBackupPath;
                        break;
                    }
                }
                //Iterate to the next filename/number
                continue;
            }
            //reset to 0 because name with 9 already used
            strNewBackupName = strprintf("wallet-autozdivbackup-%d.dat", 0);
            backupPath = backupDir / strNewBackupName;
            break;
        }
        //This filename is fresh, break here and backup
        break;
    }

    BackupWallet(*this, backupPath.string());
}

string CWallet::MintZerocoin(CAmount nValue, CWalletTx& wtxNew, vector<CZerocoinMint>& vMints, const CCoinControl* coinControl)
{
    // Check amount
    if (nValue <= 0)
        return _("Invalid amount");

    if (nValue + Params().Zerocoin_MintFee() > GetBalance())
        return _("Insufficient funds");

    CReserveKey reservekey(this);
    int64_t nFeeRequired;

    if (IsLocked()) {
        string strError = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("MintZerocoin() : %s", strError.c_str());
        return strError;
    }

    string strError;
    CMutableTransaction txNew;
    if (!CreateZerocoinMintTransaction(nValue, txNew, vMints, &reservekey, nFeeRequired, strError, coinControl)) {
        if (nValue + nFeeRequired > GetBalance())
            return strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!"), FormatMoney(nFeeRequired).c_str());
        return strError;
    }

    wtxNew = CWalletTx(this, txNew);
    wtxNew.fFromMe = true;
    wtxNew.fTimeReceivedIsTxTime = true;

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
        return _("Error: The transaction is larger than the maximum allowed transaction size!");
    }

    //commit the transaction to the network
    if (!CommitTransaction(wtxNew, reservekey)) {
        return _("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
    } else {
        //update mints with full transaction hash and then database them
        CWalletDB walletdb(pwalletMain->strWalletFile);
        for (CZerocoinMint mint : vMints) {
            mint.SetTxHash(wtxNew.GetHash());
            walletdb.WriteZerocoinMint(mint);
            pwalletMain->NotifyZerocoinChanged(pwalletMain, mint.GetValue().GetHex(), "Used", CT_UPDATED);
        }
    }

    //Create a backup of the wallet
    if (fBackupMints)
        ZDivBackupWallet();

    return "";
}

bool CWallet::SpendZerocoin(CAmount nAmount, int nSecurityLevel, CWalletTx& wtxNew, CZerocoinSpendReceipt& receipt, vector<CZerocoinMint>& vMintsSelected, bool fMintChange, bool fMinimizeChange, CBitcoinAddress* addressTo)
{
    // Default: assume something goes wrong. Depending on the problem this gets more specific below
    int nStatus = ZDIV_SPEND_ERROR;

    if (IsLocked()) {
        receipt.SetStatus("Error: Wallet locked, unable to create transaction!", ZDIV_WALLET_LOCKED);
        return false;
    }

    CReserveKey reserveKey(this);
    vector<CZerocoinMint> vNewMints;
    if (!CreateZerocoinSpendTransaction(nAmount, nSecurityLevel, wtxNew, reserveKey, receipt, vMintsSelected, vNewMints, fMintChange, fMinimizeChange, addressTo)) {
        return false;
    }

    if (fMintChange && fBackupMints)
        ZDivBackupWallet();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    if (!CommitTransaction(wtxNew, reserveKey)) {
        LogPrintf("%s: failed to commit\n", __func__);
        nStatus = ZDIV_COMMIT_FAILED;

        //reset all mints
        for (CZerocoinMint mint : vMintsSelected) {
            mint.SetUsed(false); // having error, so set to false, to be able to use again
            walletdb.WriteZerocoinMint(mint);
            pwalletMain->NotifyZerocoinChanged(pwalletMain, mint.GetValue().GetHex(), "New", CT_UPDATED);
        }

        //erase spends
        for (CZerocoinSpend spend : receipt.GetSpends()) {
            if (!walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial())) {
                receipt.SetStatus("Error: It cannot delete coin serial number in wallet", ZDIV_ERASE_SPENDS_FAILED);
            }

            //Remove from public zerocoinDB
            RemoveSerialFromDB(spend.GetSerial());
        }

        // erase new mints
        for (auto& mint : vNewMints) {
            if (!walletdb.EraseZerocoinMint(mint)) {
                receipt.SetStatus("Error: Unable to cannot delete zerocoin mint in wallet", ZDIV_ERASE_NEW_MINTS_FAILED);
            }
        }

        receipt.SetStatus("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.", nStatus);
        return false;
    }

    for (CZerocoinMint mint : vMintsSelected) {
        mint.SetUsed(true);
        if (!walletdb.WriteZerocoinMint(mint)) {
            receipt.SetStatus("Failed to write mint to db", nStatus);
            return false;
        }

        CZerocoinMint mintCheck;
        if (!walletdb.ReadZerocoinMint(mint.GetValue(), mintCheck)) {
            receipt.SetStatus("failed to read mintcheck", nStatus);
            return false;
        }

        if (!mintCheck.IsUsed()) {
            receipt.SetStatus("Error, the mint did not get marked as used", nStatus);
            return false;
        }
    }

    // write new Mints to db
    for (CZerocoinMint mint : vNewMints) {
        mint.SetTxHash(wtxNew.GetHash());
        walletdb.WriteZerocoinMint(mint);
    }

    receipt.SetStatus("Spend Successful", ZDIV_SPEND_OKAY);  // When we reach this point spending zDIV was successful

    return true;
}
