// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"

#include <primitives/transaction.h>

#include <dbenv.h>
#include "checkpoints.h"
#include <chain.h>
#include <chainparams.h>
#include "net.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "utilmoneystr.h"
#include <assert.h>
#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem/operations.hpp>
#include "FeeAndPriorityCalculator.h"
#include <blockmap.h>
#include <txmempool.h>
#include <defaultValues.h>
#include <utiltime.h>
#include <Logging.h>
#include <StakableCoin.h>
#include <SpentOutputTracker.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <I_CoinSelectionAlgorithm.h>
#include <MerkleTxConfirmationNumberCalculator.h>
#include <random.h>
#include <VaultManager.h>
#include <VaultManagerDatabase.h>

#include "Settings.h"
extern Settings& settings;

const FeeAndPriorityCalculator& priorityFeeCalculator = FeeAndPriorityCalculator::instance();

extern CCriticalSection cs_main;
extern CTxMemPool mempool;

bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(cs_main);
    // Time based nLockTime implemented in 0.1.6
    if (tx.nLockTime == 0)
        return true;
    if (nBlockHeight == 0)
        nBlockHeight = activeChain.Height();
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH (const CTxIn& txin, tx.vin)
            if (!txin.IsFinal())
            return false;
    return true;
}

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction& tx, const CChain& activeChain, int flags = -1)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses activeChain_.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than activeChain_.Height().
    const int nBlockHeight = activeChain.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST) ? activeChain.Tip()->GetMedianTimePast() : GetAdjustedTime();

    return IsFinalTx(tx, activeChain, nBlockHeight, nBlockTime);
}

bool IsAvailableType(const CKeyStore& keystore, const CScript& scriptPubKey, AvailableCoinsType coinType, isminetype& mine,VaultType& vaultType)
{
    mine = ::IsMine(keystore, scriptPubKey, vaultType);
    if( coinType == STAKABLE_COINS && vaultType == OWNED_VAULT)
    {
        return false;
    }
    else if( coinType == ALL_SPENDABLE_COINS && vaultType != NON_VAULT)
    {
        return false;
    }
    else if( coinType == OWNED_VAULT_COINS && vaultType != OWNED_VAULT)
    {
        return false;
    }
    return true;
}
bool IsAvailableType(const CKeyStore& keystore, const CScript& scriptPubKey, AvailableCoinsType coinType)
{
    VaultType vaultType;
    isminetype recoveredOwnershipType;
    return IsAvailableType(keystore,scriptPubKey,coinType,recoveredOwnershipType,vaultType);
}
bool FilterAvailableTypeByOwnershipType(const CKeyStore& keystore, const CScript& scriptPubKey, AvailableCoinsType coinType, isminetype requiredOwnershipType)
{
    VaultType vaultType;
    isminetype recoveredOwnershipType;
    if(!IsAvailableType(keystore,scriptPubKey,coinType,recoveredOwnershipType,vaultType))
    {
        return false;
    }
    return requiredOwnershipType == recoveredOwnershipType;
}

bool WriteTxToDisk(const CWallet* walletPtr, const CWalletTx& transactionToWrite)
{
    return CWalletDB(settings, walletPtr->dbFilename()).WriteTx(transactionToWrite.GetHash(),transactionToWrite);
}

const AddressBook& AddressBookManager::GetAddressBook() const
{
    return mapAddressBook;
}
AddressBook& AddressBookManager::ModifyAddressBook()
{
    return mapAddressBook;
}
bool AddressBookManager::SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& strPurpose)
{
    bool fUpdated = false;
    std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
    fUpdated = mi != mapAddressBook.end();
    mapAddressBook[address].name = strName;
    if (!strPurpose.empty()) /* update purpose only if requested */
        mapAddressBook[address].purpose = strPurpose;

    return fUpdated;
}

std::set<CTxDestination> AddressBookManager::GetAccountAddresses(std::string strAccount) const
{
    std::set<CTxDestination> result;
    BOOST_FOREACH (const PAIRTYPE(CTxDestination, CAddressBookData) & item, mapAddressBook) {
        const CTxDestination& address = item.first;
        const std::string& strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}


TransactionCreationRequest::TransactionCreationRequest(
    const std::vector<std::pair<CScript, CAmount> >& scriptsToSendTo,
    TxTextMetadata metadataToSet,
    AvailableCoinsType coinTypeSelected,
    const I_CoinSelectionAlgorithm* const algorithm
    ): scriptsToFund(scriptsToSendTo)
    , coin_type(coinTypeSelected)
    , coinSelectionAlgorithm(algorithm)
    , metadata(metadataToSet)
{
}

TransactionCreationResult::TransactionCreationResult(
    ): transactionCreationSucceeded(false)
    , errorMessage("")
    , wtxNew(nullptr)
    , reserveKey(nullptr)
{
}
TransactionCreationResult::~TransactionCreationResult()
{
    wtxNew.reset();
    reserveKey.reset();
}
TransactionCreationResult::TransactionCreationResult(TransactionCreationResult&& other)
{
    transactionCreationSucceeded = other.transactionCreationSucceeded;
    errorMessage = other.errorMessage;
    wtxNew.reset(other.wtxNew.release());
    reserveKey.reset(other.reserveKey.release());
}


CWallet::CWallet(
    const CChain& chain,
    const BlockMap& blockMap,
    const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator
    ): cs_wallet()
    , fFileBacked(false)
    , strWalletFile()
    , activeChain_(chain)
    , blockIndexByHash_(blockMap)
    , confirmationNumberCalculator_(confirmationNumberCalculator)
    , vaultDB_()
    , vaultManager_()
    , transactionRecord_(new WalletTransactionRecord(cs_wallet,strWalletFile) )
    , outputTracker_( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) )
    , pwalletdbEncryption()
    , nWalletVersion(FEATURE_BASE)
    , nWalletMaxVersion(FEATURE_BASE)
    , mapKeyMetadata()
    , mapMasterKeys()
    , nMasterKeyMaxID(0)
    , setLockedCoins()
    , mapHdPubKeys()
    , vchDefaultKey()
    , nTimeFirstKey(0)
    , timeOfLastChainTipUpdate(0)
    , nNextResend(0)
    , nLastResend(0)
    , setInternalKeyPool()
    , setExternalKeyPool()
    , walletStakingOnly(false)
    , allowSpendingZeroConfirmationOutputs(false)
    , defaultKeyPoolTopUp(0)
{
    SetNull();
}

CWallet::CWallet(
    const std::string& strWalletFileIn,
    const CChain& chain,
    const BlockMap& blockMap,
    const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator
    ): CWallet(chain, blockMap,confirmationNumberCalculator)
{
    strWalletFile = strWalletFileIn;
    fFileBacked = true;
}

CWallet::~CWallet()
{
    pwalletdbEncryption.reset();
    outputTracker_.reset();
    transactionRecord_.reset();
    vaultManager_.reset();
    vaultDB_.reset();
}

std::shared_ptr<I_WalletDatabase> CWallet::GetDatabaseBackend() const
{
    return std::shared_ptr<I_WalletDatabase>{new CWalletDB(settings,strWalletFile)};
}

bool CWallet::GetBlockLocator(CBlockLocator& blockLocator)
{
    CWalletDB walletdb(settings,strWalletFile);
    return walletdb.ReadBestBlock(blockLocator);
}

void CWallet::activateVaultMode()
{
    if(!vaultManager_)
    {
        const std::string keystring = vchDefaultKey.GetID().ToString();
        const std::string vaultID = std::string("vault_") + Hash160(keystring.begin(),keystring.end()).ToString().substr(0,10);
        vaultDB_.reset(new VaultManagerDatabase(vaultID,0));
        vaultManager_.reset(new VaultManager(confirmationNumberCalculator_,*vaultDB_));
        for(const std::string& whitelistedVaultScript: settings.GetMultiParameter("-whitelisted_vault"))
        {
            auto byteVector = ParseHex(whitelistedVaultScript);
            CScript whitelistedScript(byteVector.begin(),byteVector.end());
            assert(HexStr(ToByteVector(whitelistedScript)) == whitelistedVaultScript);
            vaultManager_->addWhiteListedScript(whitelistedScript);
        }
    }
}

int64_t CWallet::getTimestampOfFistKey() const
{
    return nTimeFirstKey;
}

void CWallet::SetNull()
{
    nWalletVersion = FEATURE_BASE;
    nWalletMaxVersion = FEATURE_BASE;
    fFileBacked = false;
    nMasterKeyMaxID = 0;
    pwalletdbEncryption.reset();
    nNextResend = 0;
    nLastResend = 0;
    nTimeFirstKey = 0;
    walletStakingOnly = false;

}

bool CWallet::isBackedByFile() const
{
    return fFileBacked;
}
const std::string CWallet::dbFilename() const
{
    return strWalletFile;
}

CKeyMetadata CWallet::getKeyMetadata(const CBitcoinAddress& address) const
{
    CKeyID keyID;
    auto it = address.GetKeyID(keyID) ? mapKeyMetadata.find(keyID) : mapKeyMetadata.end();
    CKeyMetadata metadata;
    if (it == mapKeyMetadata.end())
    {
        CScript scriptPubKey = GetScriptForDestination(address.Get());
        it = mapKeyMetadata.find(CKeyID(CScriptID(scriptPubKey)));
    }
    if (it != mapKeyMetadata.end())
    {
        metadata = it->second;
    }

    CHDChain hdChainCurrent;
    if (!keyID.IsNull() && mapHdPubKeys.count(keyID) && GetHDChain(hdChainCurrent))
    {
        metadata.isHDPubKey = true;
        metadata.hdkeypath = mapHdPubKeys.find(keyID)->second.GetKeyPath();
        metadata.hdchainid = hdChainCurrent.GetID().GetHex();
    }
    return metadata;
}

bool CWallet::LoadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey)
{
    if (mapMasterKeys.count(masterKeyIndex) != 0) {
        return false;
    }
    mapMasterKeys[masterKeyIndex] = masterKey;
    if (nMasterKeyMaxID < masterKeyIndex)
        nMasterKeyMaxID = masterKeyIndex;

    return true;
}

bool CWallet::LoadKey(const CKey& key, const CPubKey& pubkey)
{
    return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
}

bool CWallet::VerifyHDKeys() const
{
    for(const auto& entry : mapHdPubKeys)
    {
        CKey derivedKey;
        if(!GetKey(entry.first, derivedKey)) {
            return false;
        }

        if(!derivedKey.VerifyPubKey(entry.second.extPubKey.pubkey)) {
            return false;
        }
    }
    return true;
}


const CPubKey& CWallet::GetDefaultKey() const
{
    return vchDefaultKey;
}

bool CWallet::InitializeDefaultKey()
{
    CPubKey newDefaultKey;
    if (GetKeyFromPool(newDefaultKey, false))
    {
        SetDefaultKey(newDefaultKey,true);
        if (!SetAddressBook(vchDefaultKey.GetID(), "", "receive")) {
            return false;
        }
    }
    else
    {
        return false;
    }
    return true;
}

void CWallet::SetDefaultKeyTopUp(int64_t keypoolTopUp)
{
    defaultKeyPoolTopUp = keypoolTopUp;
}

void CWallet::toggleSpendingZeroConfirmationOutputs()
{
    allowSpendingZeroConfirmationOutputs = !allowSpendingZeroConfirmationOutputs;
}
const I_MerkleTxConfirmationNumberCalculator& CWallet::getConfirmationCalculator() const
{
    return confirmationNumberCalculator_;
}


bool CWallet::CanSupportFeature(enum WalletFeature wf)
{
    AssertLockHeld(cs_wallet);
    return nWalletMaxVersion >= wf;
}

isminetype CWallet::IsMine(const CScript& scriptPubKey) const
{
    return ::IsMine(*this, scriptPubKey);
}
isminetype CWallet::IsMine(const CTxDestination& dest) const
{
    return ::IsMine(*this, dest);
}
isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return IsMine(txout.scriptPubKey);
}
bool CWallet::AllInputsAreMine(const CWalletTx& walletTransaction) const
{
    bool allInputsAreMine = true;
    for (const CTxIn& txin : walletTransaction.vin) {
        isminetype mine = IsMine(txin);
        allInputsAreMine &= static_cast<bool>(mine == isminetype::ISMINE_SPENDABLE);
    }
    return allInputsAreMine;
}

CAmount CWallet::ComputeCredit(const CTxOut& txout, const UtxoOwnershipFilter& filter) const
{
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    if (!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
        throw std::runtime_error("CWallet::ComputeCredit() : value out of range");
    return ( filter.hasRequested(IsMine(txout)) ? txout.nValue : 0);
}
CAmount CWallet::ComputeChange(const CTxOut& txout) const
{
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    if (!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
        throw std::runtime_error("CWallet::ComputeChange() : value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}
bool CWallet::IsMine(const CTransaction& tx) const
{
    BOOST_FOREACH (const CTxOut& txout, tx.vout)
        if (IsMine(txout) != isminetype::ISMINE_NO)
            return true;
    return false;
}

bool CWallet::DebitsFunds(const CTransaction& tx) const
{
    return (ComputeDebit(tx, isminetype::ISMINE_SPENDABLE) > 0);
}
bool CWallet::DebitsFunds(const CWalletTx& tx,const UtxoOwnershipFilter& filter) const
{
    return GetDebit(tx,filter) > 0;
}

CAmount CWallet::ComputeDebit(const CTransaction& tx, const UtxoOwnershipFilter& filter) const
{
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    CAmount nDebit = 0;
    BOOST_FOREACH (const CTxIn& txin, tx.vin) {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeDebit() : value out of range");
    }
    return nDebit;
}
CAmount CWallet::GetDebit(const CWalletTx& tx, const UtxoOwnershipFilter& filter) const
{
    if (tx.vin.empty())
        return 0;

    CAmount debit = 0;
    if (filter.hasRequested(isminetype::ISMINE_SPENDABLE)) {
        if (tx.fDebitCached)
            debit += tx.nDebitCached;
        else {
            tx.nDebitCached = ComputeDebit(tx, isminetype::ISMINE_SPENDABLE);
            tx.fDebitCached = true;
            debit += tx.nDebitCached;
        }
    }
    if (filter.hasRequested(isminetype::ISMINE_WATCH_ONLY)) {
        if (tx.fWatchDebitCached)
            debit += tx.nWatchDebitCached;
        else {
            tx.nWatchDebitCached = ComputeDebit(tx, isminetype::ISMINE_WATCH_ONLY);
            tx.fWatchDebitCached = true;
            debit += tx.nWatchDebitCached;
        }
    }
    return debit;
}

CAmount CWallet::ComputeCredit(const CWalletTx& tx, const UtxoOwnershipFilter& filter, int creditFilterFlags) const
{
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    CAmount nCredit = 0;
    uint256 hash = tx.GetHash();
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        if( (creditFilterFlags & REQUIRE_UNSPENT) && IsSpent(tx,i)) continue;
        if( (creditFilterFlags & REQUIRE_UNLOCKED) && IsLockedCoin(hash,i)) continue;
        if( (creditFilterFlags & REQUIRE_LOCKED) && !IsLockedCoin(hash,i)) continue;

        const CTxOut& out = tx.vout[i];
        if( (creditFilterFlags & REQUIRE_AVAILABLE_TYPE) )
        {
            AvailableCoinsType coinType = static_cast<AvailableCoinsType>( creditFilterFlags >> 4);
            if(!IsAvailableType(*this,out.scriptPubKey, coinType))
            {
                continue;
            }
        }

        nCredit += ComputeCredit(out, filter);
        if (!MoneyRange(nCredit,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeCredit() : value out of range");
    }
    return nCredit;
}

CAmount CWallet::GetCredit(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (walletTransaction.IsCoinBase() && confirmationNumberCalculator_.GetBlocksToMaturity(walletTransaction) > 0)
        return 0;

    CAmount credit = 0;
    if (filter.hasRequested(isminetype::ISMINE_SPENDABLE)) {
        // GetBalance can assume transactions in mapWallet won't change
        if (walletTransaction.fCreditCached)
            credit += walletTransaction.nCreditCached;
        else {
            walletTransaction.nCreditCached = ComputeCredit(walletTransaction, isminetype::ISMINE_SPENDABLE);
            walletTransaction.fCreditCached = true;
            credit += walletTransaction.nCreditCached;
        }
    }
    if (filter.hasRequested(isminetype::ISMINE_WATCH_ONLY)) {
        if (walletTransaction.fWatchCreditCached)
            credit += walletTransaction.nWatchCreditCached;
        else {
            walletTransaction.nWatchCreditCached = ComputeCredit(walletTransaction, isminetype::ISMINE_WATCH_ONLY);
            walletTransaction.fWatchCreditCached = true;
            credit += walletTransaction.nWatchCreditCached;
        }
    }
    return credit;
}

CAmount CWallet::ComputeChange(const CTransaction& tx) const
{
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    CAmount nChange = 0;
    BOOST_FOREACH (const CTxOut& txout, tx.vout) {
        nChange += ComputeChange(txout);
        if (!MoneyRange(nChange,maxMoneyAllowedInOutput))
            throw std::runtime_error("CWallet::ComputeChange() : value out of range");
    }
    return nChange;
}

int CWallet::GetVersion()
{
    LOCK(cs_wallet);
    return nWalletVersion;
}

struct CompareValueOnly {
    bool operator()(const COutput& t1,
                    const COutput& t2) const
    {
        return t1.Value() < t2.Value();
    }
};

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    return transactionRecord_->GetWalletTx(hash);
}
std::vector<const CWalletTx*> CWallet::GetWalletTransactionReferences() const
{
    LOCK(cs_wallet);
    return transactionRecord_->GetWalletTransactionReferences();
}


CPubKey CWallet::GenerateNewKey(uint32_t nAccountIndex, bool fInternal)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    CPubKey pubkey;
    // use HD key derivation if HD was enabled during wallet creation
    if (IsHDEnabled()) {
        DeriveNewChildKey(metadata, secret, nAccountIndex, fInternal);
        pubkey = secret.GetPubKey();
    } else {
        secret.MakeNewKey(fCompressed);

        // Compressed public keys were introduced in version 0.6.0
        if (fCompressed)
            SetMinVersion(FEATURE_COMPRPUBKEY);

        pubkey = secret.GetPubKey();
        assert(secret.VerifyPubKey(pubkey));

        // Create new metadata
        mapKeyMetadata[pubkey.GetID()] = metadata;
        UpdateTimeFirstKey(nCreationTime);

        if (!AddKeyPubKey(secret, pubkey))
            throw std::runtime_error(std::string(__func__) + ": AddKey failed");
    }
    return pubkey;
}

void CWallet::DeriveNewChildKey(const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal)
{
    CHDChain hdChainTmp;
    if (!GetHDChain(hdChainTmp)) {
        throw std::runtime_error(std::string(__func__) + ": GetHDChain failed");
    }

    if (!DecryptHDChain(hdChainTmp))
        throw std::runtime_error(std::string(__func__) + ": DecryptHDChainSeed failed");
    // make sure seed matches this chain
    if (hdChainTmp.GetID() != hdChainTmp.GetSeedHash())
        throw std::runtime_error(std::string(__func__) + ": Wrong HD chain!");

    CHDAccount acc;
    if (!hdChainTmp.GetAccount(nAccountIndex, acc))
        throw std::runtime_error(std::string(__func__) + ": Wrong HD account!");

    // derive child key at next index, skip keys already known to the wallet
    CExtKey childKey;
    uint32_t nChildIndex = fInternal ? acc.nInternalChainCounter : acc.nExternalChainCounter;
    do {
        hdChainTmp.DeriveChildExtKey(nAccountIndex, fInternal, nChildIndex, childKey);
        // increment childkey index
        nChildIndex++;
    } while (HaveKey(childKey.key.GetPubKey().GetID()));
    secretRet = childKey.key;

    CPubKey pubkey = secretRet.GetPubKey();
    assert(secretRet.VerifyPubKey(pubkey));

    // store metadata
    mapKeyMetadata[pubkey.GetID()] = metadata;
    UpdateTimeFirstKey(metadata.nCreateTime);

    // update the chain model in the database
    CHDChain hdChainCurrent;
    GetHDChain(hdChainCurrent);

    if (fInternal) {
        acc.nInternalChainCounter = nChildIndex;
    }
    else {
        acc.nExternalChainCounter = nChildIndex;
    }

    if (!hdChainCurrent.SetAccount(nAccountIndex, acc))
        throw std::runtime_error(std::string(__func__) + ": SetAccount failed");

    if (IsCrypted()) {
        if (!SetCryptedHDChain(hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": SetCryptedHDChain failed");
    }
    else {
        if (!SetHDChain(hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": SetHDChain failed");
    }

    if (!AddHDPubKey(childKey.Neuter(), fInternal))
        throw std::runtime_error(std::string(__func__) + ": AddHDPubKey failed");
}

bool CWallet::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    LOCK(cs_wallet);
    std::map<CKeyID, CHDPubKey>::const_iterator mi = mapHdPubKeys.find(address);
    if (mi != mapHdPubKeys.end())
    {
        const CHDPubKey &hdPubKey = (*mi).second;
        vchPubKeyOut = hdPubKey.extPubKey.pubkey;
        return true;
    }
    else
        return CCryptoKeyStore::GetPubKey(address, vchPubKeyOut);
}

bool CWallet::GetKey(const CKeyID &address, CKey& keyOut) const
{
    LOCK(cs_wallet);
    std::map<CKeyID, CHDPubKey>::const_iterator mi = mapHdPubKeys.find(address);
    if (mi != mapHdPubKeys.end())
    {
        // if the key has been found in mapHdPubKeys, derive it on the fly
        const CHDPubKey &hdPubKey = (*mi).second;
        CHDChain hdChainCurrent;
        if (!GetHDChain(hdChainCurrent))
            throw std::runtime_error(std::string(__func__) + ": GetHDChain failed");
        if (!DecryptHDChain(hdChainCurrent))
            throw std::runtime_error(std::string(__func__) + ": DecryptHDChainSeed failed");
        // make sure seed matches this chain
        if (hdChainCurrent.GetID() != hdChainCurrent.GetSeedHash())
            throw std::runtime_error(std::string(__func__) + ": Wrong HD chain!");

        CExtKey extkey;
        hdChainCurrent.DeriveChildExtKey(hdPubKey.nAccountIndex, hdPubKey.nChangeIndex != 0, hdPubKey.extPubKey.nChild, extkey);
        keyOut = extkey.key;

        return true;
    }
    else {
        return CCryptoKeyStore::GetKey(address, keyOut);
    }
}

bool CWallet::HaveKey(const CKeyID &address) const
{
    LOCK(cs_wallet);
    if (mapHdPubKeys.count(address) > 0)
        return true;
    return CCryptoKeyStore::HaveKey(address);
}

bool CWallet::LoadHDPubKey(const CHDPubKey &hdPubKey)
{
    AssertLockHeld(cs_wallet);

    mapHdPubKeys[hdPubKey.extPubKey.pubkey.GetID()] = hdPubKey;
    return true;
}

bool CWallet::AddHDPubKey(const CExtPubKey &extPubKey, bool fInternal)
{
    AssertLockHeld(cs_wallet);

    CHDChain hdChainCurrent;
    GetHDChain(hdChainCurrent);

    CHDPubKey hdPubKey;
    hdPubKey.extPubKey = extPubKey;
    hdPubKey.hdchainID = hdChainCurrent.GetID();
    hdPubKey.nChangeIndex = fInternal ? 1 : 0;
    mapHdPubKeys[extPubKey.pubkey.GetID()] = hdPubKey;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(extPubKey.pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;

    return CWalletDB(settings,strWalletFile).WriteHDPubKey(hdPubKey, mapKeyMetadata[extPubKey.pubkey.GetID()]);
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
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
        return CWalletDB(settings,strWalletFile).WriteKey(pubkey,
                                                 secret.GetPrivKey(),
                                                 mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey& vchPubKey,
                            const std::vector<unsigned char>& vchCryptedSecret)
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
            return CWalletDB(settings,strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

void CWallet::ReserializeTransactions(const std::vector<uint256>& transactionIDs)
{
    CWalletDB walletDB(settings,strWalletFile);
    for (uint256 hash: transactionIDs)
    {
        const CWalletTx* txPtr = GetWalletTx(hash);
        if(!txPtr)
        {
            LogPrintf("Trying to write unknown transaction to database!\nHash: %s", hash);
        }
        else
        {
            walletDB.WriteTx(hash, *txPtr );
        }
    }
}

void CWallet::AddKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata)
{
    LoadKeyMetadata(pubkey,metadata,false);
}

bool CWallet::LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& meta, const bool updateFirstKeyTimestamp)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey) && updateFirstKeyTimestamp)
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    if(updateFirstKeyTimestamp) UpdateTimeFirstKey(meta.nCreateTime);
    return true;
}

bool CWallet::LoadMinVersion(int nVersion)
{
    AssertLockHeld(cs_wallet);
    nWalletVersion = nVersion;
    nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);
    return true;
}

void CWallet::UpdateTimeFirstKey(int64_t nCreateTime)
{
    AssertLockHeld(cs_wallet);
    if (nCreateTime <= 1) {
        // Cannot determine birthday information, so set the wallet birthday to
        // the beginning of time.
        nTimeFirstKey = 1;
    } else if (!nTimeFirstKey || nCreateTime < nTimeFirstKey) {
        nTimeFirstKey = nCreateTime;
    }
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
    return CWalletDB(settings,strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
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

bool CWallet::AddVault(
    const CScript& vaultScript,
    const CBlock* pblock,
    const CTransaction& tx)
{
    if(vaultManager_)
    {
        LOCK(cs_wallet);
        vaultManager_->addManagedScript(vaultScript);
        vaultManager_->addTransaction(tx, pblock, true,vaultScript);
        return true;
    }
    return false;
}
bool CWallet::RemoveVault(const CScript& vaultScript)
{
    if(vaultManager_)
    {
        LOCK(cs_wallet);
        vaultManager_->removeManagedScript(vaultScript);
        return true;
    }
    return false;
}


bool CWallet::AddWatchOnly(const CScript& dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(settings,strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(settings,strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript& dest)
{
    // Watch-only addresses have no birthday information for now,
    // so set the wallet birthday to the beginning of time.
    UpdateTimeFirstKey(1);
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
    return CWalletDB(settings,strWalletFile).WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveMultiSig(dest))
        return false;
    if (!HaveMultiSig())
        NotifyMultiSigChanged(false);
    if (fFileBacked)
        if (!CWalletDB(settings,strWalletFile).EraseMultiSig(dest))
            return false;

    return true;
}

bool CWallet::LoadMultiSig(const CScript& dest)
{
    // MultiSig addresses have no birthday information for now,
    // so set the wallet birthday to the beginning of time.
    UpdateTimeFirstKey(1);
    return CCryptoKeyStore::AddMultiSig(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool stakingOnly)
{
    SecureString strWalletPassphraseFinal;

    if (IsFullyUnlocked()) {
        walletStakingOnly = stakingOnly;
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
                walletStakingOnly = stakingOnly;
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
                CWalletDB(settings,strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();

                return true;
            }
        }
    }

    return false;
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
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(settings,strWalletFile);
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

std::set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    AssertLockHeld(cs_wallet);

    const CWalletTx* txPtr = GetWalletTx(txid);
    if (txPtr == nullptr)
        return std::set<uint256>();

   return outputTracker_->GetConflictingTxHashes(*txPtr);
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const CWalletTx& wtx, unsigned int n) const
{
    return outputTracker_->IsSpent(wtx.GetHash(), n);
}
bool CWallet::IsFullySpent(const CWalletTx& wtx) const
{
    for(unsigned outputIndex = 0; outputIndex < wtx.vout.size(); ++outputIndex)
    {
        if(!outputTracker_->IsSpent(wtx.GetHash(), outputIndex)) return false;
    }
    return true;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetStrongRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetStrongRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

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
            pwalletdbEncryption.reset(new CWalletDB(settings,strWalletFile));
            if (!pwalletdbEncryption->TxnBegin()) {
                pwalletdbEncryption.reset();
                return false;
            }
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        // must get current HD chain before EncryptKeys
        CHDChain hdChainCurrent;
        GetHDChain(hdChainCurrent);

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked) {
                pwalletdbEncryption->TxnAbort();
                pwalletdbEncryption.reset();
            }
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        if (!hdChainCurrent.IsNull()) {
            assert(EncryptHDChain(vMasterKey));

            CHDChain hdChainCrypted;
            assert(GetHDChain(hdChainCrypted));

            // ids should match, seed hashes should not
            assert(hdChainCurrent.GetID() == hdChainCrypted.GetID());
            assert(hdChainCurrent.GetSeedHash() != hdChainCrypted.GetSeedHash());

            assert(SetCryptedHDChain(hdChainCrypted, false));
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption.get(), true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit()) {
                pwalletdbEncryption.reset();
                // We now have keys encrypted in memory, but not on disk...
                // die to avoid confusion and let the user reload the unencrypted wallet.
                assert(false);
            }

            pwalletdbEncryption.reset();
        }

        Lock();
        Unlock(strWalletPassphrase);

        // if we are not using HD, generate new keypool
        if(IsHDEnabled()) {
            TopUpKeyPool();
        }
        else {
            NewKeyPool();
        }

        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CWalletDB(settings,strWalletFile).RewriteWallet();
    }
    NotifyStatusChanged(this);

    return true;
}

CWallet::TxItems CWallet::OrderedTxItems() const
{
    AssertLockHeld(cs_wallet); // mapWallet
    // First: get all CWalletTx into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); ++it)
    {
        const CWalletTx* wtx = &((*it).second);
        txOrdered.insert(std::make_pair(wtx->nOrderPos, wtx));
        assert(wtx->GetHash()==(*it).first);
    }
    return txOrdered;
}

int64_t CWallet::SmartWalletTxTimestampEstimation(const CWalletTx& wtx)
{
    int64_t latestNow = wtx.nTimeReceived;
    int64_t latestEntry = 0;
    {
        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
        int64_t latestTolerated = latestNow + 300;
        TxItems txOrdered = OrderedTxItems();
        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
            const CWalletTx* const pwtx = (*it).second;
            if (pwtx == &wtx || pwtx == nullptr)
                continue;
            int64_t nSmartTime = pwtx->nTimeSmart;
            if (!nSmartTime)
                nSmartTime = pwtx->nTimeReceived;
            if (nSmartTime <= latestTolerated)
            {
                latestEntry = nSmartTime;
                if (nSmartTime > latestNow)
                    latestNow = nSmartTime;
                break;
            }
        }
    }

    const int64_t blocktime = blockIndexByHash_.at(wtx.hashBlock)->GetBlockTime();
    return std::max(latestEntry, std::min(blocktime, latestNow));
}

void CWallet::LoadWalletTransaction(const CWalletTx& wtxIn)
{
    outputTracker_->UpdateSpends(wtxIn, true).first->RecomputeCachedQuantities();
}

void CWallet::PruneWallet()
{
    LOCK2(cs_main,cs_wallet);
    constexpr int64_t minimumNumberOfConfs = 20;
    const int64_t maxNumberOfConfs = std::max(settings.GetArg("-prunewalletconfs", minimumNumberOfConfs),minimumNumberOfConfs);
    const unsigned totalTxs = transactionRecord_->size();
    std::vector<CWalletTx> transactionsToKeep;
    transactionsToKeep.reserve(1024);
    for(const auto& wtxByHash: transactionRecord_->mapWallet)
    {
        const CWalletTx& walletTx = wtxByHash.second;
        const int64_t numberOfConfs = confirmationNumberCalculator_.FindConfirmedBlockIndexAndDepth(walletTx).second;
        if( numberOfConfs > -1 && (numberOfConfs < maxNumberOfConfs || !IsFullySpent(walletTx)))
        {
            transactionsToKeep.push_back(walletTx);
        }
    }
    outputTracker_.reset();
    transactionRecord_.reset();

    transactionRecord_.reset(new PrunedWalletTransactionRecord(cs_wallet,strWalletFile,totalTxs));
    outputTracker_.reset( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) );
    for(const CWalletTx& reloadedTransaction: transactionsToKeep)
    {
        LoadWalletTransaction(reloadedTransaction);
    }
    settings.ForceRemoveArg("-prunewalletconfs");
    LogPrintf("Prunned wallet transactions loaded to memory: Original %u vs. Current %u\n",totalTxs,transactionsToKeep.size());
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn,bool blockDisconnection)
{
    uint256 hash = wtxIn.GetHash();
    LOCK(cs_wallet);
    // Inserts only if not already there, returns tx inserted or tx found
    std::pair<CWalletTx*, bool> walletTxAndRecordStatus = outputTracker_->UpdateSpends(wtxIn,false);
    CWalletTx& wtx = *walletTxAndRecordStatus.first;
    wtx.RecomputeCachedQuantities();
    bool transactionHashIsNewToWallet = walletTxAndRecordStatus.second;

    bool walletTransactionHasBeenUpdated = false;
    if (transactionHashIsNewToWallet)
    {
        wtx.nTimeSmart = wtx.nTimeReceived;
        if (wtxIn.hashBlock != 0)
        {
            if (blockIndexByHash_.count(wtxIn.hashBlock))
            {
                wtx.nTimeSmart = SmartWalletTxTimestampEstimation(wtx);
            }
            else
            {
                LogPrintf("AddToWallet() : found %s in block %s not in index\n",
                            wtxIn.ToStringShort(), wtxIn.hashBlock);
            }
        }
    }
    else
    {
        walletTransactionHasBeenUpdated = wtx.UpdateTransaction(wtxIn,blockDisconnection);
    }

    //// debug print
    const std::string updateDescription =
        transactionHashIsNewToWallet ? "new":
        walletTransactionHasBeenUpdated? "update" : "";
    LogPrintf("AddToWallet %s %s\n", wtxIn.ToStringShort(), updateDescription);

    // Write to disk
    if (transactionHashIsNewToWallet || walletTransactionHasBeenUpdated)
    {
        if(!WriteTxToDisk(this,wtx))
        {
            LogPrintf("%s - Unable to write tx (%s) to disk\n",__func__,wtxIn.ToStringShort());
            return false;
        }
    }

    // Break debit/credit balance caches:
    wtx.RecomputeCachedQuantities();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(hash, (transactionHashIsNewToWallet) ? TransactionNotificationType::NEW : TransactionNotificationType::UPDATED);
    return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, const TransactionSyncType syncType)
{
    {
        AssertLockHeld(cs_wallet);
        if(vaultManager_) vaultManager_->addTransaction(tx,pblock,false);
        bool fExisted = GetWalletTx(tx.GetHash()) != nullptr;
        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || DebitsFunds(tx)) {
            CWalletTx wtx(tx);
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(*pblock);
            return AddToWallet(wtx,syncType == TransactionSyncType::BLOCK_DISCONNECT);
        }
    }
    return false;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock,const TransactionSyncType syncType)
{
    LOCK2(cs_main, cs_wallet);
    if (!AddToWalletIfInvolvingMe(tx, pblock, true,syncType))
        return; // Not one of ours

    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    BOOST_FOREACH (const CTxIn& txin, tx.vin) {
        CWalletTx* wtx = const_cast<CWalletTx*>(GetWalletTx(txin.prevout.hash));
        if (wtx != nullptr)
            wtx->RecomputeCachedQuantities();
    }
}
void CWallet::RelayWalletTransaction(const CWalletTx& walletTransaction)
{
    if (!walletTransaction.IsCoinBase() && !walletTransaction.IsCoinStake()) {
        if (confirmationNumberCalculator_.GetNumberOfBlockConfirmations(walletTransaction) == 0)
        {
            LogPrintf("Relaying wtx %s\n", walletTransaction.ToStringShort());
            RelayTransactionToAllPeers(static_cast<CTransaction>(walletTransaction));
        }
    }
}

void CWallet::UpdateBestBlockLocation()
{
    SetBestChain(activeChain_.GetLocator());
}
void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(settings,strWalletFile);
    walletdb.WriteBestBlock(loc);
}

static std::string ValueFromCAmount(const CAmount& amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    return strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
}
void CWallet::UpdatedBlockTip(const CBlockIndex *pindex)
{
    LogPrintf("%s - block %s; time = %s; balance = %s\n",
        __func__,std::to_string(pindex->nHeight), std::to_string(pindex->nTime), ValueFromCAmount(GetBalance()));
    timeOfLastChainTipUpdate = GetTime();
}

isminetype CWallet::IsMine(const CTxIn& txin) const
{
    {
        LOCK(cs_wallet);
        const CWalletTx* txPtr = GetWalletTx(txin.prevout.hash);
        if (txPtr != nullptr) {
            const CWalletTx& prev = *txPtr;
            if (txin.prevout.n < prev.vout.size())
                return IsMine(prev.vout[txin.prevout.n]);
        }
    }
    return isminetype::ISMINE_NO;
}

CAmount CWallet::GetDebit(const CTxIn& txin, const UtxoOwnershipFilter& filter) const
{
    {
        LOCK(cs_wallet);
        const CWalletTx* txPtr = GetWalletTx(txin.prevout.hash);
        if (txPtr != nullptr) {
            const CWalletTx& prev = *txPtr;
            if (txin.prevout.n < prev.vout.size())
                if (filter.hasRequested(IsMine(prev.vout[txin.prevout.n])))
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
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
    if (::IsMine(*this, txout.scriptPubKey) != isminetype::ISMINE_NO)
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if(txout.scriptPubKey.IsPayToPublicKeyHash()) {
            auto keyID = boost::get<CKeyID>(address);
            if(mapHdPubKeys.count(keyID)) {
                return mapHdPubKeys.at(keyID).nChangeIndex > 0;
            }
        }
    }
    return false;
}

CAmount CWallet::GetImmatureCredit(const CWalletTx& walletTransaction, bool fUseCache) const
{
    if ((walletTransaction.IsCoinBase() || walletTransaction.IsCoinStake()) &&
        confirmationNumberCalculator_.GetBlocksToMaturity(walletTransaction) > 0 &&
        confirmationNumberCalculator_.GetNumberOfBlockConfirmations(walletTransaction) > 0)
    {
        if (fUseCache && walletTransaction.fImmatureCreditCached)
            return walletTransaction.nImmatureCreditCached;
        walletTransaction.nImmatureCreditCached = ComputeCredit(walletTransaction, isminetype::ISMINE_SPENDABLE);
        walletTransaction.fImmatureCreditCached = true;
        return walletTransaction.nImmatureCreditCached;
    }

    return 0;
}

CAmount CWallet::GetAvailableCredit(const CWalletTx& walletTransaction, bool fUseCache) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (confirmationNumberCalculator_.GetBlocksToMaturity(walletTransaction) > 0)
        return 0;

    if (fUseCache && walletTransaction.fAvailableCreditCached)
        return walletTransaction.nAvailableCreditCached;

    CAmount nCredit = ComputeCredit(walletTransaction,isminetype::ISMINE_SPENDABLE, REQUIRE_UNSPENT);
    walletTransaction.nAvailableCreditCached = nCredit;
    walletTransaction.fAvailableCreditCached = true;
    return nCredit;
}

CAmount CWallet::GetChange(const CWalletTx& walletTransaction) const
{
    if (walletTransaction.fChangeCached)
        return walletTransaction.nChangeCached;
    walletTransaction.nChangeCached = ComputeChange(walletTransaction);
    walletTransaction.fChangeCached = true;
    return walletTransaction.nChangeCached;
}

/** @} */ // end of mapWallet


/** @defgroup Actions
 *
 * @{
 */

CAmount CWallet::GetStakingBalance() const
{
    return GetBalanceByCoinType(STAKABLE_COINS);
}

CAmount CWallet::GetSpendableBalance() const
{
    return GetBalanceByCoinType(ALL_SPENDABLE_COINS);
}

CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (IsTrusted(*pcoin))
                nTotal += GetAvailableCredit(*pcoin);
        }
        if(vaultManager_)
        {
            auto utxos = vaultManager_->getManagedUTXOs();
            for(const auto& utxo: utxos)
            {
                nTotal += utxo.Value();
            }
        }
    }

    return nTotal;
}

CAmount CWallet::GetBalanceByCoinType(AvailableCoinsType coinType) const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (IsTrusted(*pcoin))
            {
                int coinTypeEncoding = static_cast<int>(coinType) << 4;
                int additionalFilterFlags = REQUIRE_UNSPENT | REQUIRE_AVAILABLE_TYPE | coinTypeEncoding;
                if(coinType==STAKABLE_COINS) additionalFilterFlags |= REQUIRE_UNLOCKED;
                nTotal += ComputeCredit(*pcoin,isminetype::ISMINE_SPENDABLE, additionalFilterFlags);
            }

        }

        if(coinType == STAKABLE_COINS && vaultManager_)
        {
            auto utxos = vaultManager_->getManagedUTXOs();
            for(const auto& utxo: utxos)
            {
                nTotal += utxo.Value();
            }
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin, activeChain_) || (!IsTrusted(*pcoin) && confirmationNumberCalculator_.GetNumberOfBlockConfirmations(*pcoin) == 0))
                nTotal += GetAvailableCredit(*pcoin);
        }
        if(vaultManager_)
        {
            auto utxos = vaultManager_->getManagedUTXOs(VaultUTXOFilters::UNCONFIRMED);
            for(const auto& utxo: utxos)
            {
                nTotal += utxo.Value();
            }
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += GetImmatureCredit(*pcoin);
        }
        if(vaultManager_)
        {
            auto utxos = vaultManager_->getManagedUTXOs(VaultUTXOFilters::INMATURE);
            for(const auto& utxo: utxos)
            {
                nTotal += utxo.Value();
            }
        }
    }
    return nTotal;
}

/**
 * populate vCoins with vector of available COutputs.
 */
bool CWallet::SatisfiesMinimumDepthRequirements(const CWalletTx* pcoin, int& nDepth, bool fOnlyConfirmed) const
{
    if (!CheckFinalTx(*pcoin, activeChain_))
        return false;

    if (fOnlyConfirmed && !IsTrusted(*pcoin))
        return false;

    if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && confirmationNumberCalculator_.GetBlocksToMaturity(*pcoin) > 0)
        return false;

    // We should not consider coins which aren't at least in our mempool
    // It's possible for these to be conflicted via ancestors which we may never be able to detect
    nDepth = confirmationNumberCalculator_.GetNumberOfBlockConfirmations(*pcoin);
    if (nDepth == -1) return false;

    return true;
}

bool CWallet::IsAvailableForSpending(
    const CWalletTx* pcoin,
    unsigned int i,
    bool fIncludeZeroValue,
    bool& fIsSpendable,
    AvailableCoinsType coinType) const
{
    isminetype mine;
    VaultType vaultType;
    if(!IsAvailableType(*this,pcoin->vout[i].scriptPubKey,coinType,mine,vaultType))
    {
        return false;
    }

    const uint256 hash = pcoin->GetHash();

    if (IsSpent(*pcoin, i))
        return false;
    if (mine == isminetype::ISMINE_NO)
        return false;
    if (mine == isminetype::ISMINE_WATCH_ONLY)
        return false;

    if (IsLockedCoin(hash, i))
        return false;
    if (pcoin->vout[i].nValue <= 0 && !fIncludeZeroValue)
        return false;

    fIsSpendable = (mine == isminetype::ISMINE_SPENDABLE);
    return true;
}
void CWallet::AvailableCoins(
    std::vector<COutput>& vCoins,
    bool fOnlyConfirmed,
    bool fIncludeZeroValue,
    AvailableCoinsType nCoinType,
    CAmount nExactValue) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : transactionRecord_->mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;

            int nDepth = 0;
            if(!SatisfiesMinimumDepthRequirements(pcoin,nDepth,fOnlyConfirmed))
            {
                continue;
            }

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                bool found = (nExactValue>0)? pcoin->vout[i].nValue == nExactValue : true;
                if (!found) continue;

                bool fIsSpendable = false;
                if(!IsAvailableForSpending(pcoin,i,fIncludeZeroValue,fIsSpendable,nCoinType))
                {
                    continue;
                }

                vCoins.emplace_back(COutput(pcoin, i, nDepth, fIsSpendable));
            }
        }
        if(nCoinType == AvailableCoinsType::STAKABLE_COINS && vaultManager_)
        {
            std::vector<COutput> utxos = vaultManager_->getManagedUTXOs();
            for (const auto& entry : utxos)
            {
                const CWalletTx* pcoin = entry.tx;

                int nDepth = 0;
                if(!SatisfiesMinimumDepthRequirements(pcoin,nDepth,fOnlyConfirmed))
                {
                    continue;
                }
                if(settings.ParameterIsSet("-vault_min") && entry.Value() <  settings.GetArg("-vault_min",0)*COIN)
                {
                    continue;
                }
                vCoins.emplace_back(COutput(pcoin, entry.i, nDepth, entry.fSpendable));
            }
        }
    }
}

std::map<CBitcoinAddress, std::vector<COutput> > CWallet::AvailableCoinsByAddress(bool fConfirmed, CAmount maxCoinValue)
{
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, fConfirmed);

    std::map<CBitcoinAddress, std::vector<COutput> > mapCoins;
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

static void ApproximateSmallestCoinSubsetForPayment(
    std::vector<COutput> valuedCoins,
    const CAmount& initialEstimateOfBestSubsetTotalValue,
    const CAmount& nTargetValue,
    std::vector<bool>& selectedCoinStatusByIndex,
    CAmount& smallestTotalValueForSelectedSubset,
    int iterations = 1000)
{
    std::vector<bool> inclusionStatusByIndex;

    selectedCoinStatusByIndex.assign(valuedCoins.size(), true);
    smallestTotalValueForSelectedSubset = initialEstimateOfBestSubsetTotalValue;

    for (int nRep = 0; nRep < iterations && smallestTotalValueForSelectedSubset != nTargetValue; nRep++)
    {
        inclusionStatusByIndex.assign(valuedCoins.size(), false);
        CAmount totalValueOfSelectedSubset = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < valuedCoins.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? FastRandomContext().rand32() & 1 : !inclusionStatusByIndex[i])
                {
                    const CAmount outputValue = valuedCoins[i].Value();
                    totalValueOfSelectedSubset += outputValue;
                    inclusionStatusByIndex[i] = true;
                    if (totalValueOfSelectedSubset >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (totalValueOfSelectedSubset < smallestTotalValueForSelectedSubset)
                        {
                            smallestTotalValueForSelectedSubset = totalValueOfSelectedSubset;
                            selectedCoinStatusByIndex = inclusionStatusByIndex;
                        }
                        totalValueOfSelectedSubset -= outputValue;
                        inclusionStatusByIndex[i] = false;
                    }
                }
            }
        }
    }
}

bool CWallet::SelectStakeCoins(std::set<StakableCoin>& setCoins) const
{
    CAmount nTargetAmount = GetStakingBalance();
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true, false, STAKABLE_COINS);
    CAmount nAmountSelected = 0;

    for (const COutput& out : vCoins) {
        //make sure not to outrun target amount
        if (nAmountSelected + out.tx->vout[out.i].nValue > nTargetAmount)
            continue;

        const auto mit = blockIndexByHash_.find(out.tx->hashBlock);
        if(mit == blockIndexByHash_.end())
            continue;

        const int64_t nTxTime = mit->second->GetBlockTime();

        //check for min age
        if (std::max(int64_t(0),GetAdjustedTime() - nTxTime) < Params().GetMinCoinAgeForStaking())
            continue;

        //check that it is matured
        if (out.nDepth < (out.tx->IsCoinStake() ? Params().COINBASE_MATURITY() : 10))
            continue;

        //add to our stake set
        setCoins.emplace(*out.tx, COutPoint(out.tx->GetHash(), out.i), out.tx->hashBlock);
        nAmountSelected += out.tx->vout[out.i].nValue;
    }
    return true;
}

bool CWallet::HasAgedCoins()
{
    if (GetStakingBalance() <= 0)
        return false;

    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true, false, STAKABLE_COINS);

    for (const COutput& out : vCoins) {
        int64_t nTxTime = out.tx->GetTxTime();

        if (GetAdjustedTime() - nTxTime > Params().GetMinCoinAgeForStaking())
            return true;
    }

    return false;
}
bool CWallet::CanStakeCoins() const
{
    return !IsLocked() && !(GetStakingBalance() <= 0);
}

static void FilterToKeepConfirmedAndSpendableOutputs(
    const CWallet& wallet,
    int nConfMine,
    int nConfTheirs,
    std::vector<COutput>& vCoins)
{
    auto outputSuitabilityCheck = [&wallet,nConfMine,nConfTheirs](const COutput& output)
    {
        return !output.fSpendable || output.nDepth < (wallet.DebitsFunds(*output.tx,isminetype::ISMINE_SPENDABLE)? nConfMine : nConfTheirs);
    };
    vCoins.erase(std::remove_if(vCoins.begin(),vCoins.end(),outputSuitabilityCheck),vCoins.end());
}

bool CWallet::SelectCoinsMinConf(
    const CWallet& wallet,
    const CAmount& nTargetValue,
    int nConfMine,
    int nConfTheirs,
    std::vector<COutput> vCoins,
    std::set<COutput>& setCoinsRet,
    CAmount& nValueRet)
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    COutput coinToSpendAsFallBack;
    bool fallBackCoinWasFound = coinToSpendAsFallBack.IsValid();
    std::vector<COutput> smallValuedCoins;
    CAmount totalOfSmallValuedCoins = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);
    FilterToKeepConfirmedAndSpendableOutputs(wallet,nConfMine,nConfTheirs,vCoins);
    for(const COutput &output: vCoins)
    {
        const CAmount outputAmount = output.Value();
        if (outputAmount < nTargetValue + CENT)
        {
            smallValuedCoins.push_back(output);
            totalOfSmallValuedCoins += outputAmount;
        }
        else if (!coinToSpendAsFallBack.IsValid() || outputAmount < coinToSpendAsFallBack.Value())
        {
            coinToSpendAsFallBack = output;
            fallBackCoinWasFound = coinToSpendAsFallBack.IsValid();
        }
    }

    if (totalOfSmallValuedCoins == nTargetValue)
    {
        for (unsigned int i = 0; i < smallValuedCoins.size(); ++i)
        {
            setCoinsRet.insert(smallValuedCoins[i]);
            nValueRet += smallValuedCoins[i].Value();
        }
        return true;
    }

    if (totalOfSmallValuedCoins < nTargetValue)
    {
        if (!fallBackCoinWasFound) return false;
        setCoinsRet.insert(coinToSpendAsFallBack);
        nValueRet += coinToSpendAsFallBack.Value();
        return true;
    }

    // Solve subset sum by stochastic approximation
    std::sort(smallValuedCoins.rbegin(), smallValuedCoins.rend(), CompareValueOnly());
    std::vector<bool> selectedCoinStatusByIndex;
    CAmount totalValueOfSelectedSubset;

    ApproximateSmallestCoinSubsetForPayment(smallValuedCoins, totalOfSmallValuedCoins, nTargetValue, selectedCoinStatusByIndex, totalValueOfSelectedSubset, 1000);
    if (totalValueOfSelectedSubset != nTargetValue && totalOfSmallValuedCoins >= nTargetValue + CENT)
    {
        ApproximateSmallestCoinSubsetForPayment(smallValuedCoins, totalOfSmallValuedCoins, nTargetValue + CENT, selectedCoinStatusByIndex, totalValueOfSelectedSubset, 1000);
    }

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    const bool haveBadCoinSubset = totalValueOfSelectedSubset != nTargetValue && totalValueOfSelectedSubset < nTargetValue + CENT;
    if (fallBackCoinWasFound && (haveBadCoinSubset || coinToSpendAsFallBack.Value() <= totalValueOfSelectedSubset))
    {
        setCoinsRet.insert(coinToSpendAsFallBack);
        nValueRet += coinToSpendAsFallBack.Value();
    }
    else
    {
        for (unsigned int i = 0; i < smallValuedCoins.size(); i++)
        {
            if (selectedCoinStatusByIndex[i])
            {
                setCoinsRet.insert(smallValuedCoins[i]);
                nValueRet += smallValuedCoins[i].Value();
            }
        }
    }
    return true;
}

void AppendOutputs(
    const std::vector<std::pair<CScript, CAmount> >& intendedDestinations,
    CMutableTransaction& txNew)
{
    txNew.vout.clear();
    for(const std::pair<CScript, CAmount>& s: intendedDestinations)
    {
        txNew.vout.emplace_back(s.second,s.first);
    }
}

bool EnsureNoOutputsAreDust(const CMutableTransaction& txNew)
{
    for(const CTxOut& txout: txNew.vout)
    {
        if (priorityFeeCalculator.IsDust(txout))
        {
            return false;
        }
    }
    return true;
}

CTxOut CreateChangeOutput(CReserveKey& reservekey)
{
    CTxOut changeOutput;
    CPubKey vchPubKey;
    assert(reservekey.GetReservedKey(vchPubKey, true)); // should never fail, as we just unlocked
    changeOutput.scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
    reservekey.ReturnKey();
    return changeOutput;
}

static CAmount GetMinimumFee(unsigned int nTxBytes)
{
    const CFeeRate& feeRate = priorityFeeCalculator.getMinimumRelayFeeRate();
    return std::min(feeRate.GetFee(nTxBytes),feeRate.GetMaxTxFee());
}

//! Largest (in bytes) free transaction we're willing to create
static const unsigned int MAX_FREE_TRANSACTION_CREATE_SIZE = 1000;

static double ComputeCoinAgeOfInputs(const std::set<COutput>& outputsBeingSpent)
{
    double coinAge = 0;
    for (const COutput& output: outputsBeingSpent)
    {
        CAmount nCredit = output.Value();
        const int age = output.nDepth;
        coinAge += age==0? 0.0:(double)nCredit * (age+1);
    }
    return coinAge;
}

enum class FeeSufficiencyStatus
{
    TX_TOO_LARGE,
    NEEDS_MORE_FEES,
    HAS_ENOUGH_FEES,
};

static FeeSufficiencyStatus CheckFeesAreSufficientAndUpdateFeeAsNeeded(
    const CTransaction& wtxNew,
    const std::set<COutput> outputsBeingSpent,
    CAmount& nFeeRet)
{
    const double coinAge = ComputeCoinAgeOfInputs(outputsBeingSpent);
    CTxMemPoolEntry candidateMempoolTxEntry(wtxNew,nFeeRet,GetTime(),coinAge, 0);

    if (candidateMempoolTxEntry.GetTxSize() >= MAX_STANDARD_TX_SIZE) {
        return FeeSufficiencyStatus::TX_TOO_LARGE;
    }

    const CAmount nFeeNeeded = GetMinimumFee(candidateMempoolTxEntry.GetTxSize());
    const bool feeIsSufficient = nFeeRet >= nFeeNeeded;
    if (!feeIsSufficient)
    {
        nFeeRet = nFeeNeeded;
        return FeeSufficiencyStatus::NEEDS_MORE_FEES;
    }
    return FeeSufficiencyStatus::HAS_ENOUGH_FEES;
}

static CTransaction SignInputs(
    const CKeyStore& keyStore,
    const std::set<COutput>& setCoins,
    CMutableTransaction& txWithoutChange)
{
    // Sign
    int nIn = 0;
    for(const COutput& coin: setCoins)
    {
        if (!SignSignature(keyStore, *coin.tx, txWithoutChange, nIn++))
        {
            return CTransaction();
        }
    }
    return CTransaction(txWithoutChange);
}

static void AttachChangeOutput(
    const CTxOut& changeOutput,
    CMutableTransaction& txWithoutChange)
{
    const int changeIndex = GetRandInt(txWithoutChange.vout.size() + 1);
    txWithoutChange.vout.insert(txWithoutChange.vout.begin() + changeIndex, changeOutput);
}

static CAmount AttachInputs(
    const std::set<COutput>& setCoins,
    CMutableTransaction& txWithoutChange)
{
    // Fill vin
    CAmount nValueIn = 0;
    for(const COutput& coin: setCoins)
    {
        txWithoutChange.vin.emplace_back(coin.tx->GetHash(), coin.i);
        nValueIn += coin.Value();
    }
    return nValueIn;
}

static bool SubtractFeesFromOutputs(
    const CAmount feesToBePaid,
    CAmount& changeAmountTotal,
    CMutableTransaction& txNew)
{
    if(feesToBePaid < 1) return true;
    const CAmount totalValueSentInitially = txNew.GetValueOut();
    if(feesToBePaid > totalValueSentInitially + changeAmountTotal) return false;
    CAmount totalPaid = 0;

    for(CTxOut& out: txNew.vout)
    {
        if(totalPaid >= feesToBePaid) break;
        const CAmount minimumValue = priorityFeeCalculator.MinimumValueForNonDust(out);
        const CAmount availableBalanceToPayFees = std::max(out.nValue - minimumValue,CAmount(0));
        const CAmount change =  std::min(availableBalanceToPayFees, feesToBePaid - totalPaid);
        totalPaid += change;
        out.nValue -=  change;
    }

    const CAmount minimumValueForNonDust = priorityFeeCalculator.MinimumValueForNonDust();
    const bool feesShouldBeTakenFromChange = totalPaid < feesToBePaid;
    const bool feesCanBeTakenFromChange = (changeAmountTotal + totalPaid - feesToBePaid) >= minimumValueForNonDust;
    changeAmountTotal += totalPaid;
    if(feesShouldBeTakenFromChange)
    {
        if(!feesCanBeTakenFromChange)
        {
            return false;
        }
        changeAmountTotal -= feesToBePaid;
    }
    return changeAmountTotal >= minimumValueForNonDust && txNew.GetValueOut() == (totalValueSentInitially - feesToBePaid);
}

static bool SetChangeOutput(
    CAmount totalInputs,
    CAmount totalOutputsPlusFees,
    CMutableTransaction& txNew,
    CAmount& nFeeRet,
    CTxOut& changeOutput)
{
    changeOutput.nValue = totalInputs - totalOutputsPlusFees;
    bool changeUsed = changeOutput.nValue > 0;
    if (changeUsed && priorityFeeCalculator.IsDust(changeOutput))
    {
        nFeeRet += changeOutput.nValue;
        changeOutput.nValue = 0;
        changeUsed = false;
    }
    if(changeUsed)
    {
        AttachChangeOutput(changeOutput,txNew);
    }
    return changeUsed;
}

static std::pair<std::string,bool> SelectInputsProvideSignaturesAndFees(
    const CKeyStore& walletKeyStore,
    const I_CoinSelectionAlgorithm* coinSelector,
    const std::vector<COutput>& vCoins,
    CMutableTransaction& txNew,
    CReserveKey& reservekey,
    CWalletTx& wtxNew)
{
    CTxOut changeOutput = CreateChangeOutput(reservekey);
    const CAmount totalValueToSend = txNew.GetValueOut();
    CAmount nFeeRet = 0;
    if(!(totalValueToSend > 0))
    {
        return {translate("Transaction amounts must be positive. Total output may not exceed limits."),false};
    }
    txNew.vin.clear();
    // Choose coins to use
    std::set<COutput> setCoins = coinSelector->SelectCoins(txNew,vCoins,nFeeRet);
    CAmount nValueIn = AttachInputs(setCoins,txNew);
    CAmount totalValueToSendPlusFees = totalValueToSend + nFeeRet;
    if (setCoins.empty() || nValueIn < totalValueToSendPlusFees)
    {
        return {translate("Insufficient funds to meet coin selection algorithm requirements."),false};
    }

    bool changeUsed = SetChangeOutput(nValueIn,totalValueToSendPlusFees,txNew,nFeeRet,changeOutput);
    *static_cast<CTransaction*>(&wtxNew) = SignInputs(walletKeyStore,setCoins,txNew);
    if(wtxNew.IsNull())
    {
        return {translate("Signing transaction failed"),false};
    }

    const FeeSufficiencyStatus status = CheckFeesAreSufficientAndUpdateFeeAsNeeded(wtxNew,setCoins,nFeeRet);
    if(status == FeeSufficiencyStatus::TX_TOO_LARGE)
    {
        return {translate("Transaction too large"),false};
    }
    else if(status==FeeSufficiencyStatus::HAS_ENOUGH_FEES)
    {
        if(changeUsed)
        {
            CPubKey vchPubKey;
            assert(reservekey.GetReservedKey(vchPubKey, true));
        }
        return {std::string(""),true};
    }
    return {translate("Selected too few inputs to meet fees"),false};
}

std::pair<std::string,bool> CWallet::CreateTransaction(
    const std::vector<std::pair<CScript, CAmount> >& vecSend,
    CWalletTx& wtxNew,
    CReserveKey& reservekey,
    const I_CoinSelectionAlgorithm* coinSelector,
    AvailableCoinsType coin_type)
{
    if (vecSend.empty())
    {
        return {translate("Must provide at least one destination for funds."),false};
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.createdByMe = true;
    wtxNew.RecomputeCachedQuantities();
    CMutableTransaction txNew;

    LOCK2(cs_main, cs_wallet);
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true, false, coin_type);
    if(coinSelector == nullptr)
    {
        return {translate("Must provide a coin selection algorithm."),false};
    }

    // vouts to the payees
    AppendOutputs(vecSend,txNew);
    if(!EnsureNoOutputsAreDust(txNew))
    {
        return {translate("Transaction output(s) amount too small"),false};
    }
    return SelectInputsProvideSignaturesAndFees(*this, coinSelector,vCoins,txNew,reservekey,wtxNew);
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew);
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(settings,strWalletFile, "r") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Notify that old coins are spent
            {
                std::set<uint256> updated_hashes;
                BOOST_FOREACH (const CTxIn& txin, wtxNew.vin) {
                    // notify only once
                    if (updated_hashes.find(txin.prevout.hash) != updated_hashes.end()) continue;

                    CWalletTx* coinPtr = const_cast<CWalletTx*>(GetWalletTx(txin.prevout.hash));
                    if(!coinPtr)
                    {
                        LogPrintf("%s: Spending inputs not recorded in wallet - %s\n", __func__, txin.prevout.hash);
                        assert(coinPtr);
                    }
                    coinPtr->RecomputeCachedQuantities();
                    NotifyTransactionChanged(coinPtr->GetHash(), TransactionNotificationType::SPEND_FROM);
                    updated_hashes.insert(txin.prevout.hash);
                }
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Broadcast
        if (!SubmitTransactionToMempool(mempool,wtxNew))
        {
            // This must not fail. The transaction has already been signed and recorded.
            LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
            return false;
        }
        RelayWalletTransaction(wtxNew);
    }
    return true;
}

TransactionCreationResult CWallet::SendMoney(const TransactionCreationRequest& requestedTransaction)
{
    TransactionCreationResult result;
    result.reserveKey.reset(new CReserveKey(*this));
    result.wtxNew.reset(new CWalletTx());
    if(!requestedTransaction.metadata.empty())
    {
        CWalletTx& walletTx = *result.wtxNew;
        walletTx.mapValue = requestedTransaction.metadata;
        if(walletTx.mapValue.count("FromAccount") > 0)
        {
            walletTx.strFromAccount = walletTx.mapValue["FromAccount"];
            walletTx.mapValue.erase(walletTx.strFromAccount);
        }
    }
    std::pair<std::string,bool> createTxResult =
        CreateTransaction(
            requestedTransaction.scriptsToFund,
            *result.wtxNew,
            *result.reserveKey,
            requestedTransaction.coinSelectionAlgorithm,
            requestedTransaction.coin_type);

    result.transactionCreationSucceeded = createTxResult.second;
    result.errorMessage = createTxResult.first;
    if(!result.transactionCreationSucceeded) return std::move(result);

    result.transactionCreationSucceeded = CommitTransaction(*result.wtxNew,*result.reserveKey);
    if(!result.transactionCreationSucceeded)
    {
        return std::move(result);
    }
    return std::move(result);
}

DBErrors CWallet::LoadWallet()
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nLoadWalletRet;
    {
        LOCK(cs_wallet);
        nLoadWalletRet = CWalletDB(settings,strWalletFile,"cr+").LoadWallet(*static_cast<I_WalletLoader*>(this));
    }
    if (nLoadWalletRet == DB_REWRITTEN)
    {
        LOCK(cs_wallet);
        setInternalKeyPool.clear();
        setExternalKeyPool.clear();
        // Note: can't top-up keypool here, because wallet is locked.
        // User will be prompted to unlock wallet the next operation
        // that requires a new key.
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    nLoadWalletRet = (vchDefaultKey.IsValid())? DB_LOAD_OK_RELOAD: DB_LOAD_OK_FIRST_RUN;
    uiInterface.LoadWallet(this);
    return nLoadWalletRet;
}

bool CWallet::SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet);
        fUpdated = AddressBookManager::SetAddressBook(address,strName,strPurpose);
    }
    NotifyAddressBookChanged(address, strName, ::IsMine(*this, address) != isminetype::ISMINE_NO,
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW));
    if (!fFileBacked)
        return false;
    if (!strPurpose.empty() && !CWalletDB(settings,strWalletFile).WritePurpose(CBitcoinAddress(address).ToString(), strPurpose))
        return false;
    return CWalletDB(settings,strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

CAddressBookData& CWallet::ModifyAddressBookData(const CTxDestination& address)
{
    return ModifyAddressBook()[address];
}

bool CWallet::SetDefaultKey(const CPubKey& vchPubKey, bool updateDatabase)
{
    if (fFileBacked && updateDatabase) {
        if (!CWalletDB(settings,strWalletFile).WriteDefaultKey(vchPubKey))
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
        CWalletDB walletdb(settings,strWalletFile);
        BOOST_FOREACH(int64_t nIndex, setInternalKeyPool) {
            walletdb.ErasePool(nIndex);
        }
        setInternalKeyPool.clear();
        BOOST_FOREACH(int64_t nIndex, setExternalKeyPool) {
            walletdb.ErasePool(nIndex);
        }
        setExternalKeyPool.clear();

        if (IsLocked())
            return false;

        if (!TopUpKeyPool())
            return false;

        LogPrintf("CWallet::NewKeyPool rewrote keypool\n");
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    constexpr unsigned int DEFAULT_KEYPOOL_SIZE = 1000;
    {
        LOCK(cs_wallet);

        if (IsLocked(true))
            return false;

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = std::max(
                defaultKeyPoolTopUp? defaultKeyPoolTopUp: settings.GetArg("-keypool", DEFAULT_KEYPOOL_SIZE),
                (int64_t) 0);

        // count amount of available keys (internal, external)
        // make sure the keypool of external and internal keys fits the user selected target (-keypool)
        int64_t amountExternal = setExternalKeyPool.size();
        int64_t amountInternal = setInternalKeyPool.size();
        int64_t missingExternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - amountExternal, (int64_t) 0);
        int64_t missingInternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - amountInternal, (int64_t) 0);

        if (!IsHDEnabled())
        {
            // don't create extra internal keys
            missingInternal = 0;
        } else {
            nTargetSize *= 2;
        }
        bool fInternal = false;
        CWalletDB walletdb(settings,strWalletFile);
        for (int64_t i = missingInternal + missingExternal; i--;)
        {
            int64_t nEnd = 1;
            if (i < missingInternal) {
                fInternal = true;
            }
            if (!setInternalKeyPool.empty()) {
                nEnd = *(--setInternalKeyPool.end()) + 1;
            }
            if (!setExternalKeyPool.empty()) {
                nEnd = std::max(nEnd, *(--setExternalKeyPool.end()) + 1);
            }
            // TODO: implement keypools for all accounts?
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey(0, fInternal), fInternal)))
                throw std::runtime_error(std::string(__func__) + ": writing generated key failed");

            if (fInternal) {
                setInternalKeyPool.insert(nEnd);
            } else {
                setExternalKeyPool.insert(nEnd);
            }
            LogPrintf("keypool added key %d, size=%u, internal=%d\n", nEnd, setInternalKeyPool.size() + setExternalKeyPool.size(), fInternal);

            double dProgress = 100.f * nEnd / (nTargetSize + 1);
            std::string strMsg = strprintf(translate("Loading wallet... (%3.2f %%)"), dProgress);
            uiInterface.InitMessage(strMsg);
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fInternal)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked(true))
            TopUpKeyPool();

        fInternal = fInternal && IsHDEnabled();
        std::set<int64_t>& setKeyPool = fInternal ? setInternalKeyPool : setExternalKeyPool;

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(settings,strWalletFile);

        nIndex = *setKeyPool.begin();
        setKeyPool.erase(nIndex);
        if (!walletdb.ReadPool(nIndex, keypool)) {
            throw std::runtime_error(std::string(__func__) + ": read failed");
        }
        if (!HaveKey(keypool.vchPubKey.GetID())) {
            throw std::runtime_error(std::string(__func__) + ": unknown key in key pool");
        }
        if (keypool.fInternal != fInternal) {
            throw std::runtime_error(std::string(__func__) + ": keypool entry misclassified");
        }

        assert(keypool.vchPubKey.IsValid());
        LogPrintf("keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked) {
        CWalletDB walletdb(settings,strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex, bool fInternal)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        if (fInternal) {
            setInternalKeyPool.insert(nIndex);
        } else {
            setExternalKeyPool.insert(nIndex);
        }
    }
    LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result, bool fInternal)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool, fInternal);
        if (nIndex == -1) {
            if (IsLocked()) return false;
            result = GenerateNewKey(0, fInternal);
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

static int64_t GetOldestKeyInPool(const std::set<int64_t>& setKeyPool, CWalletDB& walletdb) {
    CKeyPool keypool;
    int64_t nIndex = *(setKeyPool.begin());
    if (!walletdb.ReadPool(nIndex, keypool)) {
        throw std::runtime_error(std::string(__func__) + ": read oldest key in keypool failed");
    }
    assert(keypool.vchPubKey.IsValid());
    return keypool.nTime;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    LOCK(cs_wallet);

    // if the keypool is empty, return <NOW>
    if (setExternalKeyPool.empty() && setInternalKeyPool.empty())
        return GetTime();

    CWalletDB walletdb(settings,strWalletFile);
    int64_t oldestKey = -1;

    // load oldest key from keypool, get time and return
    if (!setInternalKeyPool.empty()) {
        oldestKey = std::max(GetOldestKeyInPool(setInternalKeyPool, walletdb), oldestKey);
    }
    if (!setExternalKeyPool.empty()) {
        oldestKey = std::max(GetOldestKeyInPool(setExternalKeyPool, walletdb), oldestKey);
    }
    return oldestKey;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH (PAIRTYPE(uint256, CWalletTx) walletEntry, transactionRecord_->mapWallet) {
            CWalletTx* pcoin = &walletEntry.second;

            if (!IsFinalTx(*pcoin, activeChain_) || !IsTrusted(*pcoin))
                continue;

            if (pcoin->IsCoinBase() && confirmationNumberCalculator_.GetBlocksToMaturity(*pcoin) > 0)
                continue;

            int nDepth = confirmationNumberCalculator_.GetNumberOfBlockConfirmations(*pcoin);
            if (nDepth < ( DebitsFunds(*pcoin,isminetype::ISMINE_SPENDABLE) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                CTxDestination addr;
                if (IsMine(pcoin->vout[i]) == isminetype::ISMINE_NO)
                    continue;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.second, i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

std::set<std::set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    std::set<std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    BOOST_FOREACH (PAIRTYPE(uint256, CWalletTx) walletEntry, transactionRecord_->mapWallet) {
        CWalletTx* pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0) {
            bool any_mine = false;
            // group all input addresses with each other
            BOOST_FOREACH (CTxIn txin, pcoin->vin) {
                CTxDestination address;
                if (IsMine(txin) == isminetype::ISMINE_NO) /* If this input isn't mine, ignore it */
                    continue;
                const CWalletTx* previousTxPtr = GetWalletTx(txin.prevout.hash);
                if(!previousTxPtr) continue;
                if (!ExtractDestination(previousTxPtr->vout[txin.prevout.n].scriptPubKey, address))
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
            if (IsMine(pcoin->vout[i]) != isminetype::ISMINE_NO) {
                CTxDestination address;
                if (!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set<std::set<CTxDestination>*> uniqueGroupings;        // a set of pointers to groups of addresses
    std::map<CTxDestination, std::set<CTxDestination>*> setmap; // map addresses to the unique group containing it
    BOOST_FOREACH (std::set<CTxDestination> grouping, groupings) {
        // make a set of all the groups hit by this new group
        std::set<std::set<CTxDestination>*> hits;
        std::map<CTxDestination, std::set<CTxDestination>*>::iterator it;
        BOOST_FOREACH (CTxDestination address, grouping)
                if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        std::set<CTxDestination>* merged = new std::set<CTxDestination>(grouping);
        BOOST_FOREACH (std::set<CTxDestination>* hit, hits) {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH (CTxDestination element, *merged)
                setmap[element] = merged;
    }

    std::set<std::set<CTxDestination> > ret;
    BOOST_FOREACH (std::set<CTxDestination>* uniqueGrouping, uniqueGroupings) {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

static void LoadReserveKeysToSet(std::set<CKeyID>& setAddress, const std::set<int64_t>& setKeyPool, CWalletDB& walletdb)
{
    BOOST_FOREACH(const int64_t& id, setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw std::runtime_error(std::string(__func__) + ": read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        setAddress.insert(keyID);
    }
}

void CWallet::GetAllReserveKeys(std::set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(settings,strWalletFile);

    LOCK2(cs_main, cs_wallet);
    LoadReserveKeysToSet(setAddress, setInternalKeyPool, walletdb);
    LoadReserveKeysToSet(setAddress, setExternalKeyPool, walletdb);

    BOOST_FOREACH (const CKeyID& keyID, setAddress) {
        if (!HaveKey(keyID)) {
            throw std::runtime_error(std::string(__func__) + ": unknown key in key pool");
        }
    }
}

unsigned int CWallet::GetKeyPoolSize() const
{
    LOCK(cs_wallet);
    return setInternalKeyPool.size() + setExternalKeyPool.size();
}

bool CWallet::IsTrusted(const CWalletTx& walletTransaction) const
{
    // Quick answer in most cases
    if (!IsFinalTx(walletTransaction, activeChain_))
        return false;
    int nDepth = confirmationNumberCalculator_.GetNumberOfBlockConfirmations(walletTransaction);
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!allowSpendingZeroConfirmationOutputs || !DebitsFunds(walletTransaction, isminetype::ISMINE_SPENDABLE)) // using wtx's cached debit
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    BOOST_FOREACH (const CTxIn& txin, walletTransaction.vin) {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = GetWalletTx(txin.prevout.hash);
        if (parent == NULL)
            return false;
        const CTxOut& parentOut = parent->vout[txin.prevout.n];
        if (IsMine(parentOut) != isminetype::ISMINE_SPENDABLE)
            return false;
    }
    return true;

}

void CWallet::LockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
    CWalletTx* txPtr = const_cast<CWalletTx*>(GetWalletTx(output.hash));
    if (txPtr != nullptr) txPtr->RecomputeCachedQuantities(); // recalculate all credits for this tx
}

void CWallet::UnlockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(const uint256& hash, unsigned int n) const
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

class CAffectedKeysVisitor : public boost::static_visitor<void> {
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    void Process(const CScript &script) {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            BOOST_FOREACH(const CTxDestination &dest, vDest)
                    boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
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
    CBlockIndex* pindexMax = activeChain_[std::max(0, activeChain_.Height() - 144)]; // the tip can be reorganised; use a 144-block safety margin
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
    for (std::map<uint256, CWalletTx>::const_iterator it = transactionRecord_->mapWallet.begin(); it != transactionRecord_->mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx& wtx = (*it).second;
        BlockMap::const_iterator blit = blockIndexByHash_.find(wtx.hashBlock);
        if (blit != blockIndexByHash_.end() && activeChain_.Contains(blit->second)) {
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

CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn, bool fInternalIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
    fInternal = fInternalIn;
}

void CWallet::LoadKeyPool(int nIndex, const CKeyPool &keypool)
{
    if (keypool.fInternal) {
        setInternalKeyPool.insert(nIndex);
    } else {
        setExternalKeyPool.insert(nIndex);
    }

    // If no metadata exists yet, create a default with the pool key's
    // creation time. Note that this may be overwritten by actually
    // stored metadata for that key later, which is fine.
    CKeyID keyid = keypool.vchPubKey.GetID();
    if (mapKeyMetadata.count(keyid) == 0)
        mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
}

void CWallet::GenerateNewHDChain()
{
    CHDChain newHdChain;

    std::string strSeed = settings.GetArg("-hdseed", "not hex");

    if(settings.ParameterIsSet("-hdseed") && IsHex(strSeed)) {
        std::vector<unsigned char> vchSeed = ParseHex(strSeed);
        if (!newHdChain.SetSeed(SecureVector(vchSeed.begin(), vchSeed.end()), true))
            throw std::runtime_error(std::string(__func__) + ": SetSeed failed");
    }
    else {
        if (settings.ParameterIsSet("-hdseed") && !IsHex(strSeed))
            LogPrintf("CWallet::GenerateNewHDChain -- Incorrect seed, generating random one instead\n");

        // NOTE: empty mnemonic means "generate a new one for me"
        std::string strMnemonic = settings.GetArg("-mnemonic", "");
        // NOTE: default mnemonic passphrase is an empty string
        std::string strMnemonicPassphrase = settings.GetArg("-mnemonicpassphrase", "");

        SecureVector vchMnemonic(strMnemonic.begin(), strMnemonic.end());
        SecureVector vchMnemonicPassphrase(strMnemonicPassphrase.begin(), strMnemonicPassphrase.end());

        if (!newHdChain.SetMnemonic(vchMnemonic, vchMnemonicPassphrase, true))
            throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed");
    }
    newHdChain.Debug(__func__);

    if (!SetHDChain(newHdChain, false))
        throw std::runtime_error(std::string(__func__) + ": SetHDChain failed");

    // clean up
    settings.ForceRemoveArg("-hdseed");
    settings.ForceRemoveArg("-mnemonic");
    settings.ForceRemoveArg("-mnemonicpassphrase");
}

bool CWallet::SetHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetHDChain(chain))
        return false;

    if (!memonly && !CWalletDB(settings,strWalletFile).WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": WriteHDChain failed");

    return true;
}

bool CWallet::SetCryptedHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetCryptedHDChain(chain))
        return false;

    if (!memonly) {
        if (!fFileBacked)
            return false;
        if (pwalletdbEncryption) {
            if (!pwalletdbEncryption->WriteCryptedHDChain(chain))
                throw std::runtime_error(std::string(__func__) + ": WriteCryptedHDChain failed");
        } else {
            if (!CWalletDB(settings,strWalletFile).WriteCryptedHDChain(chain))
                throw std::runtime_error(std::string(__func__) + ": WriteCryptedHDChain failed");
        }
    }

    return true;
}

bool CWallet::GetDecryptedHDChain(CHDChain& hdChainRet)
{
    LOCK(cs_wallet);

    CHDChain hdChainTmp;
    if (!GetHDChain(hdChainTmp)) {
        return false;
    }

    if (!DecryptHDChain(hdChainTmp))
        return false;

    // make sure seed matches this chain
    if (hdChainTmp.GetID() != hdChainTmp.GetSeedHash())
        return false;

    hdChainRet = hdChainTmp;

    return true;
}

bool CWallet::IsHDEnabled()
{
    CHDChain hdChainCurrent;
    return GetHDChain(hdChainCurrent);
}

bool CWallet::IsUnlockedForStakingOnly() const
{
    return !IsLocked() && walletStakingOnly;
}
bool CWallet::IsFullyUnlocked() const
{
    return !IsLocked() && !walletStakingOnly;
}
void CWallet::LockFully()
{
    LOCK(cs_wallet);
    walletStakingOnly = false;
    Lock();
}