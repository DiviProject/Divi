// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletdb.h"

#include "base58.h"
#include "protocol.h"
#include "serialize.h"
#include "sync.h"
#include "util.h"
#include "utiltime.h"
#include "wallet.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <fstream>

using namespace boost;
using namespace std;

static uint64_t nAccountingEntryNumber = 0;

//
// CWalletDB
//

bool CWalletDB::WriteName(const string& strAddress, const string& strName)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("name"), strAddress), strName);
}

bool CWalletDB::EraseName(const string& strAddress)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    nWalletDBUpdated++;
    return Erase(make_pair(string("name"), strAddress));
}

bool CWalletDB::WritePurpose(const string& strAddress, const string& strPurpose)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("purpose"), strAddress), strPurpose);
}

bool CWalletDB::ErasePurpose(const string& strPurpose)
{
    nWalletDBUpdated++;
    return Erase(make_pair(string("purpose"), strPurpose));
}

bool CWalletDB::WriteTx(uint256 hash, const CWalletTx& wtx)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("tx"), hash), wtx);
}

bool CWalletDB::EraseTx(uint256 hash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("tx"), hash));
}

bool CWalletDB::WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("keymeta"), vchPubKey),
            keyMeta, false))
        return false;

    // hash pubkey/privkey to accelerate wallet load
    std::vector<unsigned char> vchKey;
    vchKey.reserve(vchPubKey.size() + vchPrivKey.size());
    vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
    vchKey.insert(vchKey.end(), vchPrivKey.begin(), vchPrivKey.end());

    return Write(std::make_pair(std::string("key"), vchPubKey), std::make_pair(vchPrivKey, Hash(vchKey.begin(), vchKey.end())), false);
}

bool CWalletDB::WriteCryptedKey(const CPubKey& vchPubKey,
    const std::vector<unsigned char>& vchCryptedSecret,
    const CKeyMetadata& keyMeta)
{
    const bool fEraseUnencryptedKey = true;
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("keymeta"), vchPubKey),
            keyMeta))
        return false;

    if (!Write(std::make_pair(std::string("ckey"), vchPubKey), vchCryptedSecret, false))
        return false;
    if (fEraseUnencryptedKey) {
        Erase(std::make_pair(std::string("key"), vchPubKey));
        Erase(std::make_pair(std::string("wkey"), vchPubKey));
    }
    return true;
}

bool CWalletDB::WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("mkey"), nID), kMasterKey, true);
}

bool CWalletDB::WriteCScript(const uint160& hash, const CScript& redeemScript)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("cscript"), hash), redeemScript, false);
}

bool CWalletDB::WriteWatchOnly(const CScript& dest)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("watchs"), dest), '1');
}

bool CWalletDB::EraseWatchOnly(const CScript& dest)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("watchs"), dest));
}

bool CWalletDB::WriteMultiSig(const CScript& dest)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("multisig"), dest), '1');
}

bool CWalletDB::EraseMultiSig(const CScript& dest)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("multisig"), dest));
}

bool CWalletDB::WriteBestBlock(const CBlockLocator& locator)
{
    nWalletDBUpdated++;
    return Write(std::string("bestblock"), locator);
}

bool CWalletDB::ReadBestBlock(CBlockLocator& locator)
{
    return Read(std::string("bestblock"), locator);
}

bool CWalletDB::WriteOrderPosNext(int64_t nOrderPosNext)
{
    nWalletDBUpdated++;
    return Write(std::string("orderposnext"), nOrderPosNext);
}

// presstab HyperStake
bool CWalletDB::WriteStakeSplitThreshold(uint64_t nStakeSplitThreshold)
{
    nWalletDBUpdated++;
    return Write(std::string("stakeSplitThreshold"), nStakeSplitThreshold);
}

//presstab HyperStake
bool CWalletDB::WriteMultiSend(std::vector<std::pair<std::string, int> > vMultiSend)
{
    nWalletDBUpdated++;
    bool ret = true;
    for (unsigned int i = 0; i < vMultiSend.size(); i++) {
        std::pair<std::string, int> pMultiSend;
        pMultiSend = vMultiSend[i];
        if (!Write(std::make_pair(std::string("multisend"), i), pMultiSend, true))
            ret = false;
    }
    return ret;
}
//presstab HyperStake
bool CWalletDB::EraseMultiSend(std::vector<std::pair<std::string, int> > vMultiSend)
{
    nWalletDBUpdated++;
    bool ret = true;
    for (unsigned int i = 0; i < vMultiSend.size(); i++) {
        std::pair<std::string, int> pMultiSend;
        pMultiSend = vMultiSend[i];
        if (!Erase(std::make_pair(std::string("multisend"), i)))
            ret = false;
    }
    return ret;
}
//presstab HyperStake
bool CWalletDB::WriteMSettings(bool fMultiSendStake, bool fMultiSendMasternode, int nLastMultiSendHeight)
{
    nWalletDBUpdated++;
    std::pair<bool, bool> enabledMS(fMultiSendStake, fMultiSendMasternode);
    std::pair<std::pair<bool, bool>, int> pSettings(enabledMS, nLastMultiSendHeight);

    return Write(std::string("msettingsv2"), pSettings, true);
}
//presstab HyperStake
bool CWalletDB::WriteMSDisabledAddresses(std::vector<std::string> vDisabledAddresses)
{
    nWalletDBUpdated++;
    bool ret = true;
    for (unsigned int i = 0; i < vDisabledAddresses.size(); i++) {
        if (!Write(std::make_pair(std::string("mdisabled"), i), vDisabledAddresses[i]))
            ret = false;
    }
    return ret;
}
//presstab HyperStake
bool CWalletDB::EraseMSDisabledAddresses(std::vector<std::string> vDisabledAddresses)
{
    nWalletDBUpdated++;
    bool ret = true;
    for (unsigned int i = 0; i < vDisabledAddresses.size(); i++) {
        if (!Erase(std::make_pair(std::string("mdisabled"), i)))
            ret = false;
    }
    return ret;
}
bool CWalletDB::WriteAutoCombineSettings(bool fEnable, CAmount nCombineThreshold)
{
    nWalletDBUpdated++;
    std::pair<bool, CAmount> pSettings;
    pSettings.first = fEnable;
    pSettings.second = nCombineThreshold;
    return Write(std::string("autocombinesettings"), pSettings, true);
}

bool CWalletDB::WriteDefaultKey(const CPubKey& vchPubKey)
{
    nWalletDBUpdated++;
    return Write(std::string("defaultkey"), vchPubKey);
}

bool CWalletDB::ReadPool(int64_t nPool, CKeyPool& keypool)
{
    return Read(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::WritePool(int64_t nPool, const CKeyPool& keypool)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::ErasePool(int64_t nPool)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("pool"), nPool));
}

bool CWalletDB::WriteMinVersion(int nVersion)
{
    return Write(std::string("minversion"), nVersion);
}

bool CWalletDB::ReadAccount(const string& strAccount, CAccount& account)
{
    account.SetNull();
    return Read(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccount(const string& strAccount, const CAccount& account)
{
    return Write(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry)
{
    return Write(std::make_pair(std::string("acentry"), std::make_pair(acentry.strAccount, nAccEntryNum)), acentry);
}

bool CWalletDB::WriteAccountingEntry(const CAccountingEntry& acentry)
{
    return WriteAccountingEntry(++nAccountingEntryNumber, acentry);
}

CAmount CWalletDB::GetAccountCreditDebit(const string& strAccount)
{
    list<CAccountingEntry> entries;
    ListAccountCreditDebit(strAccount, entries);

    CAmount nCreditDebit = 0;
    BOOST_FOREACH (const CAccountingEntry& entry, entries)
        nCreditDebit += entry.nCreditDebit;

    return nCreditDebit;
}

void CWalletDB::ListAccountCreditDebit(const string& strAccount, list<CAccountingEntry>& entries)
{
    bool fAllAccounts = (strAccount == "*");

    Dbc* pcursor = GetCursor();
    if (!pcursor)
        throw runtime_error("CWalletDB::ListAccountCreditDebit() : cannot create DB cursor");
    unsigned int fFlags = DB_SET_RANGE;
    while (true) {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << std::make_pair(std::string("acentry"), std::make_pair((fAllAccounts ? string("") : strAccount), uint64_t(0)));
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0) {
            pcursor->close();
            throw runtime_error("CWalletDB::ListAccountCreditDebit() : error scanning DB");
        }

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType != "acentry")
            break;
        CAccountingEntry acentry;
        ssKey >> acentry.strAccount;
        if (!fAllAccounts && acentry.strAccount != strAccount)
            break;

        ssValue >> acentry;
        ssKey >> acentry.nEntryNo;
        entries.push_back(acentry);
    }

    pcursor->close();
}

DBErrors CWalletDB::ReorderTransactions(CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    // Old wallets didn't have any defined order for transactions
    // Probably a bad idea to change the output of this

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-time multimap.
    typedef pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef multimap<int64_t, TxPair> TxItems;
    TxItems txByTime;

    for (map<uint256, CWalletTx>::iterator it = pwallet->mapWallet.begin(); it != pwallet->mapWallet.end(); ++it) {
        CWalletTx* wtx = &((*it).second);
        txByTime.insert(make_pair(wtx->nTimeReceived, TxPair(wtx, (CAccountingEntry*)0)));
    }
    list<CAccountingEntry> acentries;
    ListAccountCreditDebit("", acentries);
    BOOST_FOREACH (CAccountingEntry& entry, acentries) {
        txByTime.insert(make_pair(entry.nTime, TxPair((CWalletTx*)0, &entry)));
    }

    int64_t& nOrderPosNext = pwallet->nOrderPosNext;
    nOrderPosNext = 0;
    std::vector<int64_t> nOrderPosOffsets;
    for (TxItems::iterator it = txByTime.begin(); it != txByTime.end(); ++it) {
        CWalletTx* const pwtx = (*it).second.first;
        CAccountingEntry* const pacentry = (*it).second.second;
        int64_t& nOrderPos = (pwtx != 0) ? pwtx->nOrderPos : pacentry->nOrderPos;

        if (nOrderPos == -1) {
            nOrderPos = nOrderPosNext++;
            nOrderPosOffsets.push_back(nOrderPos);

            if (pwtx) {
                if (!WriteTx(pwtx->GetHash(), *pwtx))
                    return DB_LOAD_FAIL;
            } else if (!WriteAccountingEntry(pacentry->nEntryNo, *pacentry))
                return DB_LOAD_FAIL;
        } else {
            int64_t nOrderPosOff = 0;
            BOOST_FOREACH (const int64_t& nOffsetStart, nOrderPosOffsets) {
                if (nOrderPos >= nOffsetStart)
                    ++nOrderPosOff;
            }
            nOrderPos += nOrderPosOff;
            nOrderPosNext = std::max(nOrderPosNext, nOrderPos + 1);

            if (!nOrderPosOff)
                continue;

            // Since we're changing the order, write it back
            if (pwtx) {
                if (!WriteTx(pwtx->GetHash(), *pwtx))
                    return DB_LOAD_FAIL;
            } else if (!WriteAccountingEntry(pacentry->nEntryNo, *pacentry))
                return DB_LOAD_FAIL;
        }
    }
    WriteOrderPosNext(nOrderPosNext);

    return DB_LOAD_OK;
}

class CWalletScanState
{
public:
    unsigned int nKeys;
    unsigned int nCKeys;
    unsigned int nKeyMeta;
    bool fIsEncrypted;
    bool fAnyUnordered;
    int nFileVersion;
    vector<uint256> vWalletUpgrade;

    CWalletScanState()
    {
        nKeys = nCKeys = nKeyMeta = 0;
        fIsEncrypted = false;
        fAnyUnordered = false;
        nFileVersion = 0;
    }
};

bool ReadKeyValue(CWallet* pwallet, CDataStream& ssKey, CDataStream& ssValue, CWalletScanState& wss, string& strType, string& strErr)
{
    try {
        // Unserialize
        // Taking advantage of the fact that pair serialization
        // is just the two items serialized one after the other
        ssKey >> strType;
        if (strType == "name") {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[CBitcoinAddress(strAddress).Get()].name;
        } else if (strType == "purpose") {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[CBitcoinAddress(strAddress).Get()].purpose;
        } else if (strType == "tx") {
            uint256 hash;
            ssKey >> hash;
            CWalletTx wtx;
            ssValue >> wtx;
            CValidationState state;
            // false because there is no reason to go through the zerocoin checks for our own wallet
            if (!(CheckTransaction(wtx, false, false, state) && (wtx.GetHash() == hash) && state.IsValid()))
                return false;

            // Undo serialize changes in 31600
            if (31404 <= wtx.fTimeReceivedIsTxTime && wtx.fTimeReceivedIsTxTime <= 31703) {
                if (!ssValue.empty()) {
                    char fTmp;
                    char fUnused;
                    ssValue >> fTmp >> fUnused >> wtx.strFromAccount;
                    strErr = strprintf("LoadWallet() upgrading tx ver=%d %d '%s' %s",
                        wtx.fTimeReceivedIsTxTime, fTmp, wtx.strFromAccount, hash.ToString());
                    wtx.fTimeReceivedIsTxTime = fTmp;
                } else {
                    strErr = strprintf("LoadWallet() repairing tx ver=%d %s", wtx.fTimeReceivedIsTxTime, hash.ToString());
                    wtx.fTimeReceivedIsTxTime = 0;
                }
                wss.vWalletUpgrade.push_back(hash);
            }

            if (wtx.nOrderPos == -1)
                wss.fAnyUnordered = true;

            pwallet->AddToWallet(wtx, true);
        } else if (strType == "acentry") {
            string strAccount;
            ssKey >> strAccount;
            uint64_t nNumber;
            ssKey >> nNumber;
            if (nNumber > nAccountingEntryNumber)
                nAccountingEntryNumber = nNumber;

            if (!wss.fAnyUnordered) {
                CAccountingEntry acentry;
                ssValue >> acentry;
                if (acentry.nOrderPos == -1)
                    wss.fAnyUnordered = true;
            }
        } else if (strType == "watchs") {
            CScript script;
            ssKey >> script;
            char fYes;
            ssValue >> fYes;
            if (fYes == '1')
                pwallet->LoadWatchOnly(script);

            // Watch-only addresses have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        } else if (strType == "multisig") {
            CScript script;
            ssKey >> script;
            char fYes;
            ssValue >> fYes;
            if (fYes == '1')
                pwallet->LoadMultiSig(script);

            // MultiSig addresses have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        } else if (strType == "key" || strType == "wkey") {
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            if (!vchPubKey.IsValid()) {
                strErr = "Error reading wallet database: CPubKey corrupt";
                return false;
            }
            CKey key;
            CPrivKey pkey;
            uint256 hash = 0;

            if (strType == "key") {
                wss.nKeys++;
                ssValue >> pkey;
            } else {
                CWalletKey wkey;
                ssValue >> wkey;
                pkey = wkey.vchPrivKey;
            }

            // Old wallets store keys as "key" [pubkey] => [privkey]
            // ... which was slow for wallets with lots of keys, because the public key is re-derived from the private key
            // using EC operations as a checksum.
            // Newer wallets store keys as "key"[pubkey] => [privkey][hash(pubkey,privkey)], which is much faster while
            // remaining backwards-compatible.
            try {
                ssValue >> hash;
            } catch (...) {
            }

            bool fSkipCheck = false;

            if (hash != 0) {
                // hash pubkey/privkey to accelerate wallet load
                std::vector<unsigned char> vchKey;
                vchKey.reserve(vchPubKey.size() + pkey.size());
                vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
                vchKey.insert(vchKey.end(), pkey.begin(), pkey.end());

                if (Hash(vchKey.begin(), vchKey.end()) != hash) {
                    strErr = "Error reading wallet database: CPubKey/CPrivKey corrupt";
                    return false;
                }

                fSkipCheck = true;
            }

            if (!key.Load(pkey, vchPubKey, fSkipCheck)) {
                strErr = "Error reading wallet database: CPrivKey corrupt";
                return false;
            }
            if (!pwallet->LoadKey(key, vchPubKey)) {
                strErr = "Error reading wallet database: LoadKey failed";
                return false;
            }
        } else if (strType == "mkey") {
            unsigned int nID;
            ssKey >> nID;
            CMasterKey kMasterKey;
            ssValue >> kMasterKey;
            if (pwallet->mapMasterKeys.count(nID) != 0) {
                strErr = strprintf("Error reading wallet database: duplicate CMasterKey id %u", nID);
                return false;
            }
            pwallet->mapMasterKeys[nID] = kMasterKey;
            if (pwallet->nMasterKeyMaxID < nID)
                pwallet->nMasterKeyMaxID = nID;
        } else if (strType == "ckey") {
            vector<unsigned char> vchPubKey;
            ssKey >> vchPubKey;
            vector<unsigned char> vchPrivKey;
            ssValue >> vchPrivKey;
            wss.nCKeys++;

            if (!pwallet->LoadCryptedKey(vchPubKey, vchPrivKey)) {
                strErr = "Error reading wallet database: LoadCryptedKey failed";
                return false;
            }
            wss.fIsEncrypted = true;
        } else if (strType == "keymeta") {
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            CKeyMetadata keyMeta;
            ssValue >> keyMeta;
            wss.nKeyMeta++;

            pwallet->LoadKeyMetadata(vchPubKey, keyMeta);

            // find earliest key creation time, as wallet birthday
            if (!pwallet->nTimeFirstKey ||
                (keyMeta.nCreateTime < pwallet->nTimeFirstKey))
                pwallet->nTimeFirstKey = keyMeta.nCreateTime;
        } else if (strType == "defaultkey") {
            ssValue >> pwallet->vchDefaultKey;
        } else if (strType == "pool") {
            int64_t nIndex;
            ssKey >> nIndex;
            CKeyPool keypool;
            ssValue >> keypool;
            pwallet->setKeyPool.insert(nIndex);

            // If no metadata exists yet, create a default with the pool key's
            // creation time. Note that this may be overwritten by actually
            // stored metadata for that key later, which is fine.
            CKeyID keyid = keypool.vchPubKey.GetID();
            if (pwallet->mapKeyMetadata.count(keyid) == 0)
                pwallet->mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
        } else if (strType == "version") {
            ssValue >> wss.nFileVersion;
            if (wss.nFileVersion == 10300)
                wss.nFileVersion = 300;
        } else if (strType == "cscript") {
            uint160 hash;
            ssKey >> hash;
            CScript script;
            ssValue >> script;
            if (!pwallet->LoadCScript(script)) {
                strErr = "Error reading wallet database: LoadCScript failed";
                return false;
            }
        } else if (strType == "orderposnext") {
            ssValue >> pwallet->nOrderPosNext;
        } else if (strType == "stakeSplitThreshold") //presstab HyperStake
        {
            ssValue >> pwallet->nStakeSplitThreshold;
        } else if (strType == "multisend") //presstab HyperStake
        {
            unsigned int i;
            ssKey >> i;
            std::pair<std::string, int> pMultiSend;
            ssValue >> pMultiSend;
            if (CBitcoinAddress(pMultiSend.first).IsValid()) {
                pwallet->vMultiSend.push_back(pMultiSend);
            }
        } else if (strType == "msettingsv2") //presstab HyperStake
        {
            std::pair<std::pair<bool, bool>, int> pSettings;
            ssValue >> pSettings;
            pwallet->fMultiSendStake = pSettings.first.first;
            pwallet->fMultiSendMasternodeReward = pSettings.first.second;
            pwallet->nLastMultiSendHeight = pSettings.second;
        } else if (strType == "mdisabled") //presstab HyperStake
        {
            std::string strDisabledAddress;
            ssValue >> strDisabledAddress;
            pwallet->vDisabledAddresses.push_back(strDisabledAddress);
        } else if (strType == "autocombinesettings") {
            std::pair<bool, CAmount> pSettings;
            ssValue >> pSettings;
            pwallet->fCombineDust = pSettings.first;
            pwallet->nAutoCombineThreshold = pSettings.second;
        } else if (strType == "destdata") {
            std::string strAddress, strKey, strValue;
            ssKey >> strAddress;
            ssKey >> strKey;
            ssValue >> strValue;
            if (!pwallet->LoadDestData(CBitcoinAddress(strAddress).Get(), strKey, strValue)) {
                strErr = "Error reading wallet database: LoadDestData failed";
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

static bool IsKeyType(string strType)
{
    return (strType == "key" || strType == "wkey" ||
            strType == "mkey" || strType == "ckey");
}

DBErrors CWalletDB::LoadWallet(CWallet* pwallet)
{
    pwallet->vchDefaultKey = CPubKey();
    CWalletScanState wss;
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;

    try {
        LOCK(pwallet->cs_wallet);
        int nMinVersion = 0;
        if (Read((string) "minversion", nMinVersion)) {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
            pwallet->LoadMinVersion(nMinVersion);
        }

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor) {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true) {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0) {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            // Try to be tolerant of single corrupt records:
            string strType, strErr;
            if (!ReadKeyValue(pwallet, ssKey, ssValue, wss, strType, strErr)) {
                // losing keys is considered a catastrophic error, anything else
                // we assume the user can live with:
                if (IsKeyType(strType))
                    result = DB_CORRUPT;
                else {
                    // Leave other errors alone, if we try to fix them we might make things worse.
                    fNoncriticalErrors = true; // ... but do warn the user there is something wrong.
                    if (strType == "tx")
                        // Rescan if there is a bad transaction record:
                        SoftSetBoolArg("-rescan", true);
                }
            }
            if (!strErr.empty())
                LogPrintf("%s\n", strErr);
        }
        pcursor->close();
    } catch (boost::thread_interrupted) {
        throw;
    } catch (...) {
        result = DB_CORRUPT;
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    // Any wallet corruption at all: skip any rewriting or
    // upgrading, we don't want to make it worse.
    if (result != DB_LOAD_OK)
        return result;

    LogPrintf("nFileVersion = %d\n", wss.nFileVersion);

    LogPrintf("Keys: %u plaintext, %u encrypted, %u w/ metadata, %u total\n",
        wss.nKeys, wss.nCKeys, wss.nKeyMeta, wss.nKeys + wss.nCKeys);

    // nTimeFirstKey is only reliable if all keys have metadata
    if ((wss.nKeys + wss.nCKeys) != wss.nKeyMeta)
        pwallet->nTimeFirstKey = 1; // 0 would be considered 'no value'

    BOOST_FOREACH (uint256 hash, wss.vWalletUpgrade)
        WriteTx(hash, pwallet->mapWallet[hash]);

    // Rewrite encrypted wallets of versions 0.4.0 and 0.5.0rc:
    if (wss.fIsEncrypted && (wss.nFileVersion == 40000 || wss.nFileVersion == 50000))
        return DB_NEED_REWRITE;

    if (wss.nFileVersion < CLIENT_VERSION) // Update
        WriteVersion(CLIENT_VERSION);

    if (wss.fAnyUnordered)
        result = ReorderTransactions(pwallet);

    return result;
}

DBErrors CWalletDB::FindWalletTx(CWallet* pwallet, vector<uint256>& vTxHash, vector<CWalletTx>& vWtx)
{
    pwallet->vchDefaultKey = CPubKey();
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;

    try {
        LOCK(pwallet->cs_wallet);
        int nMinVersion = 0;
        if (Read((string) "minversion", nMinVersion)) {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
            pwallet->LoadMinVersion(nMinVersion);
        }

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor) {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true) {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0) {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            string strType;
            ssKey >> strType;
            if (strType == "tx") {
                uint256 hash;
                ssKey >> hash;

                CWalletTx wtx;
                ssValue >> wtx;

                vTxHash.push_back(hash);
                vWtx.push_back(wtx);
            }
        }
        pcursor->close();
    } catch (boost::thread_interrupted) {
        throw;
    } catch (...) {
        result = DB_CORRUPT;
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    return result;
}

DBErrors CWalletDB::ZapWalletTx(CWallet* pwallet, vector<CWalletTx>& vWtx)
{
    // build list of wallet TXs
    vector<uint256> vTxHash;
    DBErrors err = FindWalletTx(pwallet, vTxHash, vWtx);
    if (err != DB_LOAD_OK)
        return err;

    // erase each wallet TX
    BOOST_FOREACH (uint256& hash, vTxHash) {
        if (!EraseTx(hash))
            return DB_CORRUPT;
    }

    return DB_LOAD_OK;
}

void ThreadFlushWalletDB(const string& strFile)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("divi-wallet");

    static bool fOneThread;
    if (fOneThread)
        return;
    fOneThread = true;
    if (!GetBoolArg("-flushwallet", true))
        return;

    unsigned int nLastSeen = nWalletDBUpdated;
    unsigned int nLastFlushed = nWalletDBUpdated;
    int64_t nLastWalletUpdate = GetTime();
    while (true) {
        MilliSleep(500);

        if (nLastSeen != nWalletDBUpdated) {
            nLastSeen = nWalletDBUpdated;
            nLastWalletUpdate = GetTime();
        }

        if (nLastFlushed != nWalletDBUpdated && GetTime() - nLastWalletUpdate >= 2) {
            TRY_LOCK(bitdb.cs_db, lockDb);
            if (lockDb) {
                // Don't do this if any databases are in use
                int nRefCount = 0;
                map<string, int>::iterator mi = bitdb.mapFileUseCount.begin();
                while (mi != bitdb.mapFileUseCount.end()) {
                    nRefCount += (*mi).second;
                    mi++;
                }

                if (nRefCount == 0) {
                    boost::this_thread::interruption_point();
                    map<string, int>::iterator mi = bitdb.mapFileUseCount.find(strFile);
                    if (mi != bitdb.mapFileUseCount.end()) {
                        LogPrint("db", "Flushing wallet.dat\n");
                        nLastFlushed = nWalletDBUpdated;
                        int64_t nStart = GetTimeMillis();

                        // Flush wallet.dat so it's self contained
                        bitdb.CloseDb(strFile);
                        bitdb.CheckpointLSN(strFile);

                        bitdb.mapFileUseCount.erase(mi++);
                        LogPrint("db", "Flushed wallet.dat %dms\n", GetTimeMillis() - nStart);
                    }
                }
            }
        }
    }
}

bool BackupWallet(const CWallet& wallet, const string& strDest)
{
    if (!wallet.fFileBacked)
        return false;
    while (true) {
        {
            LOCK(bitdb.cs_db);
            if (!bitdb.mapFileUseCount.count(wallet.strWalletFile) || bitdb.mapFileUseCount[wallet.strWalletFile] == 0) {
                // Flush log data to the dat file
                bitdb.CloseDb(wallet.strWalletFile);
                bitdb.CheckpointLSN(wallet.strWalletFile);
                bitdb.mapFileUseCount.erase(wallet.strWalletFile);

                // Copy wallet.dat
                filesystem::path pathSrc = GetDataDir() / wallet.strWalletFile;
                filesystem::path pathDest(strDest);
                if (filesystem::is_directory(pathDest))
                    pathDest /= wallet.strWalletFile;

                try {
#if BOOST_VERSION >= 158000
                    filesystem::copy_file(pathSrc, pathDest, filesystem::copy_option::overwrite_if_exists);
#else
                    std::ifstream src(pathSrc.string(), std::ios::binary);
                    std::ofstream dst(pathDest.string(), std::ios::binary);
                    dst << src.rdbuf();
#endif
                    LogPrintf("copied wallet.dat to %s\n", pathDest.string());
                    return true;
                } catch (const filesystem::filesystem_error& e) {
                    LogPrintf("error copying wallet.dat to %s - %s\n", pathDest.string(), e.what());
                    return false;
                }
            }
        }
        MilliSleep(100);
    }
    return false;
}

//
// Try to (very carefully!) recover wallet.dat if there is a problem.
//
bool CWalletDB::Recover(CDBEnv& dbenv, std::string filename, bool fOnlyKeys)
{
    // Recovery procedure:
    // move wallet.dat to wallet.timestamp.bak
    // Call Salvage with fAggressive=true to
    // get as much data as possible.
    // Rewrite salvaged data to wallet.dat
    // Set -rescan so any missing transactions will be
    // found.
    int64_t now = GetTime();
    std::string newFilename = strprintf("wallet.%d.bak", now);

    int result = dbenv.dbenv.dbrename(NULL, filename.c_str(), NULL,
        newFilename.c_str(), DB_AUTO_COMMIT);
    if (result == 0)
        LogPrintf("Renamed %s to %s\n", filename, newFilename);
    else {
        LogPrintf("Failed to rename %s to %s\n", filename, newFilename);
        return false;
    }

    std::vector<CDBEnv::KeyValPair> salvagedData;
    bool allOK = dbenv.Salvage(newFilename, true, salvagedData);
    if (salvagedData.empty()) {
        LogPrintf("Salvage(aggressive) found no records in %s.\n", newFilename);
        return false;
    }
    LogPrintf("Salvage(aggressive) found %u records\n", salvagedData.size());

    bool fSuccess = allOK;
    boost::scoped_ptr<Db> pdbCopy(new Db(&dbenv.dbenv, 0));
    int ret = pdbCopy->open(NULL, // Txn pointer
        filename.c_str(),         // Filename
        "main",                   // Logical db name
        DB_BTREE,                 // Database type
        DB_CREATE,                // Flags
        0);
    if (ret > 0) {
        LogPrintf("Cannot create database file %s\n", filename);
        return false;
    }
    CWallet dummyWallet;
    CWalletScanState wss;

    DbTxn* ptxn = dbenv.TxnBegin();
    BOOST_FOREACH (CDBEnv::KeyValPair& row, salvagedData) {
        if (fOnlyKeys) {
            CDataStream ssKey(row.first, SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(row.second, SER_DISK, CLIENT_VERSION);
            string strType, strErr;
            bool fReadOK = ReadKeyValue(&dummyWallet, ssKey, ssValue,
                wss, strType, strErr);
            if (!IsKeyType(strType))
                continue;
            if (!fReadOK) {
                LogPrintf("WARNING: CWalletDB::Recover skipping %s: %s\n", strType, strErr);
                continue;
            }
        }
        Dbt datKey(&row.first[0], row.first.size());
        Dbt datValue(&row.second[0], row.second.size());
        int ret2 = pdbCopy->put(ptxn, &datKey, &datValue, DB_NOOVERWRITE);
        if (ret2 > 0)
            fSuccess = false;
    }
    ptxn->commit(0);
    pdbCopy->close(0);

    return fSuccess;
}

bool CWalletDB::Recover(CDBEnv& dbenv, std::string filename)
{
    return CWalletDB::Recover(dbenv, filename, false);
}

bool CWalletDB::WriteDestData(const std::string& address, const std::string& key, const std::string& value)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("destdata"), std::make_pair(address, key)), value);
}

bool CWalletDB::EraseDestData(const std::string& address, const std::string& key)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("destdata"), std::make_pair(address, key)));
}

bool CWalletDB::WriteZerocoinSpendSerialEntry(const CZerocoinSpend& zerocoinSpend)
{
    return Write(make_pair(string("zcserial"), zerocoinSpend.GetSerial()), zerocoinSpend, true);
}
bool CWalletDB::EraseZerocoinSpendSerialEntry(const CBigNum& serialEntry)
{
    return Erase(make_pair(string("zcserial"), serialEntry));
}

bool CWalletDB::ReadZerocoinSpendSerialEntry(const CBigNum& bnSerial)
{
    CZerocoinSpend spend;
    return Read(make_pair(string("zcserial"), bnSerial), spend);
}

bool CWalletDB::WriteZerocoinMint(const CZerocoinMint& zerocoinMint)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << zerocoinMint.GetValue();
    uint256 hash = Hash(ss.begin(), ss.end());

    Erase(make_pair(string("zerocoin"), hash));
    return Write(make_pair(string("zerocoin"), hash), zerocoinMint, true);
}

bool CWalletDB::ReadZerocoinMint(const CBigNum &bnPubCoinValue, CZerocoinMint& zerocoinMint)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnPubCoinValue;
    uint256 hash = Hash(ss.begin(), ss.end());

    return Read(make_pair(string("zerocoin"), hash), zerocoinMint);
}

bool CWalletDB::EraseZerocoinMint(const CZerocoinMint& zerocoinMint)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << zerocoinMint.GetValue();
    uint256 hash = Hash(ss.begin(), ss.end());

    return Erase(make_pair(string("zerocoin"), hash));
}

bool CWalletDB::ArchiveMintOrphan(const CZerocoinMint& zerocoinMint)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << zerocoinMint.GetValue();
    uint256 hash = Hash(ss.begin(), ss.end());;

    if (!Write(make_pair(string("zco"), hash), zerocoinMint)) {
        LogPrintf("%s : failed to database orphaned zerocoin mint\n", __func__);
        return false;
    }

    if (!Erase(make_pair(string("zerocoin"), hash))) {
        LogPrintf("%s : failed to erase orphaned zerocoin mint\n", __func__);
        return false;
    }

    return true;
}

bool CWalletDB::UnarchiveZerocoin(const CZerocoinMint& mint)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << mint.GetValue();
    uint256 hash = Hash(ss.begin(), ss.end());;

    if (!Erase(make_pair(string("zco"), hash))) {
        LogPrintf("%s : failed to erase archived zerocoin mint\n", __func__);
        return false;
    }

    return WriteZerocoinMint(mint);
}

std::list<CZerocoinMint> CWalletDB::ListMintedCoins(bool fUnusedOnly, bool fMaturedOnly, bool fUpdateStatus)
{
    std::list<CZerocoinMint> listPubCoin;
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        throw runtime_error(std::string(__func__)+" : cannot create DB cursor");
    unsigned int fFlags = DB_SET_RANGE;
    vector<CZerocoinMint> vOverWrite;
    vector<CZerocoinMint> vArchive;
    for (;;)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("zerocoin"), uint256(0));
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
        {
            pcursor->close();
            throw runtime_error(std::string(__func__)+" : error scanning DB");
        }

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType != "zerocoin")
            break;

        uint256 value;
        ssKey >> value;

        CZerocoinMint mint;
        ssValue >> mint;

        if (fUnusedOnly) {
            if (mint.IsUsed())
                continue;

            //double check that we have no record of this serial being used
            if (ReadZerocoinSpendSerialEntry(mint.GetSerialNumber())) {
                mint.SetUsed(true);
                vOverWrite.emplace_back(mint);
                continue;
            }
        }

        if (fMaturedOnly || fUpdateStatus) {
            //if there is not a record of the block height, then look it up and assign it
            if (!mint.GetHeight()) {
                CTransaction tx;
                uint256 hashBlock;
                if(!GetTransaction(mint.GetTxHash(), tx, hashBlock, true)) {
                    LogPrintf("%s failed to find tx for mint txid=%s\n", __func__, mint.GetTxHash().GetHex());
                    vArchive.emplace_back(mint);
                    continue;
                }

                //if not in the block index, most likely is unconfirmed tx
                if (mapBlockIndex.count(hashBlock)) {
                    mint.SetHeight(mapBlockIndex[hashBlock]->nHeight);
                    vOverWrite.emplace_back(mint);
                } else if (fMaturedOnly){
                    continue;
                }
            }

            //not mature
            if (mint.GetHeight() > chainActive.Height() - Params().Zerocoin_MintRequiredConfirmations()) {
                if (!fMaturedOnly)
                    listPubCoin.emplace_back(mint);
                continue;
            }

            //if only requesting an update (fUpdateStatus) then skip the rest and add to list
            if (fMaturedOnly) {
                // check to make sure there are at least 3 other mints added to the accumulators after this
                if (chainActive.Height() < mint.GetHeight() + 1)
                    continue;

                CBlockIndex *pindex = chainActive[mint.GetHeight() + 1];
                int nMintsAdded = 0;
                while(pindex->nHeight < chainActive.Height() - 30) { // 30 just to make sure that its at least 2 checkpoints from the top block
                    nMintsAdded += count(pindex->vMintDenominationsInBlock.begin(), pindex->vMintDenominationsInBlock.end(), mint.GetDenomination());
                    if(nMintsAdded >= Params().Zerocoin_RequiredAccumulation())
                        break;
                    pindex = chainActive[pindex->nHeight + 1];
                }

                if(nMintsAdded < Params().Zerocoin_RequiredAccumulation())
                    continue;
            }
        }
        listPubCoin.emplace_back(mint);
    }

    pcursor->close();

    //overwrite any updates
    for (CZerocoinMint mint : vOverWrite) {
        if(!this->WriteZerocoinMint(mint))
            LogPrintf("%s failed to update mint from tx %s\n", __func__, mint.GetTxHash().GetHex());
    }

    // archive mints
    for (CZerocoinMint mint : vArchive) {
        if (!this->ArchiveMintOrphan(mint))
            LogPrintf("%s failed to archive mint from %s\n", __func__, mint.GetTxHash().GetHex());
    }

    return listPubCoin;
}
// Just get the Serial Numbers
std::list<CBigNum> CWalletDB::ListMintedCoinsSerial()
{
    std::list<CBigNum> listPubCoin;
    std::list<CZerocoinMint> listCoins = ListMintedCoins(true, false, false);
    
    for ( auto& coin : listCoins) {
        listPubCoin.push_back(coin.GetSerialNumber());
    }
    return listPubCoin;
}


std::list<CZerocoinSpend> CWalletDB::ListSpentCoins()
{
    std::list<CZerocoinSpend> listCoinSpend;
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        throw runtime_error(std::string(__func__)+" : cannot create DB cursor");
    unsigned int fFlags = DB_SET_RANGE;
    for (;;)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("zcserial"), CBigNum(0));
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
        {
            pcursor->close();
            throw runtime_error(std::string(__func__)+" : error scanning DB");
        }

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType != "zcserial")
            break;

        CBigNum value;
        ssKey >> value;

        CZerocoinSpend zerocoinSpendItem;
        ssValue >> zerocoinSpendItem;

        listCoinSpend.push_back(zerocoinSpendItem);
    }

    pcursor->close();
    return listCoinSpend;
}

// Just get the Serial Numbers
std::list<CBigNum> CWalletDB::ListSpentCoinsSerial()
{
    std::list<CBigNum> listPubCoin;
    std::list<CZerocoinSpend> listCoins = ListSpentCoins();
    
    for ( auto& coin : listCoins) {
        listPubCoin.push_back(coin.GetSerial());
    }
    return listPubCoin;
}

std::list<CZerocoinMint> CWalletDB::ListArchivedZerocoins()
{
    std::list<CZerocoinMint> listMints;
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        throw runtime_error(std::string(__func__)+" : cannot create DB cursor");
    unsigned int fFlags = DB_SET_RANGE;
    for (;;)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("zco"), CBigNum(0));
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
        {
            pcursor->close();
            throw runtime_error(std::string(__func__)+" : error scanning DB");
        }

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType != "zco")
            break;

        uint256 value;
        ssKey >> value;

        CZerocoinMint mint;
        ssValue >> mint;

        listMints.push_back(mint);
    }

    pcursor->close();
    return listMints;
}

