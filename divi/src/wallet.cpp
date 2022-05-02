// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"

#include <walletdb.h>
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
#include <defaultValues.h>
#include <MemPoolEntry.h>
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
#include <BlockScanner.h>
#include <ui_interface.h>

#include <stack>

#include "Settings.h"
extern Settings& settings;

const FeeAndPriorityCalculator& priorityFeeCalculator = FeeAndPriorityCalculator::instance();

extern CCriticalSection cs_main;

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

bool IsAvailableType(const CKeyStore& keystore, const CScript& scriptPubKey, AvailableCoinsType coinType, isminetype& mine,VaultType& vaultType)
{
    mine = ::IsMine(keystore, scriptPubKey, vaultType);
    if( coinType == AvailableCoinsType::STAKABLE_COINS && vaultType == OWNED_VAULT)
    {
        return false;
    }
    else if( coinType == AvailableCoinsType::ALL_SPENDABLE_COINS && vaultType != NON_VAULT)
    {
        return false;
    }
    else if( coinType == AvailableCoinsType::OWNED_VAULT_COINS && vaultType != OWNED_VAULT)
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

AddressBookManager::AddressBookManager(): mapAddressBook(), destinationByLabel_()
{
}

const AddressBookManager::LastDestinationByLabel& AddressBookManager::GetLastDestinationByLabel() const
{
    return destinationByLabel_;
}

const AddressBook& AddressBookManager::GetAddressBook() const
{
    return mapAddressBook;
}
bool AddressBookManager::SetAddressLabel(const CTxDestination& address, const std::string newLabel)
{
    bool updatesExistingLabel = mapAddressBook.find(address) != mapAddressBook.end();
    mapAddressBook[address].name = newLabel;
    destinationByLabel_[newLabel] = address;
    return updatesExistingLabel;
}

TransactionCreationRequest::TransactionCreationRequest(
    const std::vector<std::pair<CScript, CAmount> >& scriptsToSendTo,
    TransactionFeeMode txFeeMode,
    TxTextMetadata metadataToSet,
    AvailableCoinsType coinTypeSelected,
    const I_CoinSelectionAlgorithm* const algorithm
    ): scriptsToFund(scriptsToSendTo)
    , transactionFeeMode(txFeeMode)
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
    , addressBookManager_(new AddressBookManager())
    , vaultManager_()
    , transactionRecord_(new WalletTransactionRecord(cs_wallet) )
    , outputTracker_( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) )
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
    outputTracker_.reset();
    transactionRecord_.reset();
    vaultManager_.reset();
}

void CWallet::InitializeDatabaseBackend()
{
    AssertLockHeld(cs_wallet);
    CWalletDB(settings,strWalletFile,"cr+");
}

std::unique_ptr<I_WalletDatabase> CWallet::GetDatabaseBackend() const
{
    assert(fFileBacked);
    return std::unique_ptr<I_WalletDatabase>{fFileBacked? new CWalletDB(settings,strWalletFile): nullptr};
}

void CWallet::activateVaultMode(
    std::shared_ptr<VaultManager> vaultManager)
{
    if(!vaultManager_)
    {
        vaultManager_ = vaultManager;
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

static const CBlockIndex* ApproximateFork(const CChain& chain,const BlockMap& blockIndexByHash, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for(const uint256& hash: locator.vHave) {
        const auto mi = blockIndexByHash.find(hash);
        if (mi != blockIndexByHash.end()) {
            const CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

const CBlockIndex* CWallet::GetNextUnsycnedBlockIndexInMainChain(bool syncFromGenesis)
{
    CBlockLocator locator;
    const bool forceSyncFromGenesis = settings.GetBoolArg("-force_rescan",false);
    if( forceSyncFromGenesis || (!syncFromGenesis && !(fFileBacked && GetDatabaseBackend()->ReadBestBlock(locator)) ) ) syncFromGenesis = true;
    const CBlockIndex* pindex = syncFromGenesis? activeChain_.Genesis():ApproximateFork(activeChain_,blockIndexByHash_, locator);
    const int64_t timestampOfFirstKey = getTimestampOfFistKey();
    while (!forceSyncFromGenesis && pindex && timestampOfFirstKey && (pindex->GetBlockTime() < (timestampOfFirstKey - 7200)))
        pindex = activeChain_.Next(pindex);

    return pindex;
}

static int computeProgress(int currentHeight,int startHeight,int endHeight)
{
    const int numerator = (currentHeight - startHeight) * 100;
    const int denominator = (endHeight - startHeight + 1) ;
    const int progress = numerator / denominator;
    return std::max(1, std::min(99, progress));
}

void CWallet::verifySyncToActiveChain(const I_BlockDataReader& blockReader, bool startFromGenesis)
{
    LOCK2(cs_main,cs_wallet);
    const CBlockIndex* const startingBlockIndex = GetNextUnsycnedBlockIndexInMainChain(startFromGenesis);
    if(!startingBlockIndex) return;

    BlockScanner blockScanner(blockReader, activeChain_,startingBlockIndex);

    const int endHeight = activeChain_.Tip()->nHeight;
    const int startHeight = startingBlockIndex->nHeight;
    const unsigned numberOfBlocksToScan = endHeight - startHeight +1;
    unsigned currentHeight = startingBlockIndex->nHeight;
    int64_t nNow = GetTime();

    const std::string typeOfScanMessage = startFromGenesis? "Rescanning" : "Scanning";
    LogPrintf("%s... from height %d to %d\n",typeOfScanMessage, startHeight,endHeight);
    while(blockScanner.advanceToNextBlock() && numberOfBlocksToScan > 0u)
    {
        if (currentHeight % 100 == 0)
        {
            const int progress = computeProgress(currentHeight,startHeight,endHeight);
            LogPrintf("%s...%d%%\n",typeOfScanMessage, progress);
        }
        SyncTransactions(blockScanner.blockTransactions(), &blockScanner.blockRef(), TransactionSyncType::RESCAN);
        if (GetTime() >= nNow + 60)
        {
            nNow = GetTime();
            LogPrintf("%s - at block %d. Progress=%d%%\n",typeOfScanMessage, currentHeight, computeProgress(currentHeight,startHeight,endHeight));
        }
        currentHeight += 1;
    }
    LogPrintf("%s...done\n",typeOfScanMessage);

    SetBestChain(activeChain_.GetLocator());
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


std::string CWallet::getWalletIdentifier() const
{
    return vchDefaultKey.GetID().ToString();
}

bool CWallet::LoadDefaultKey(const CPubKey& vchPubKey, bool updateDatabase)
{
    if (updateDatabase) {
        if (fFileBacked && !GetDatabaseBackend()->WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

bool CWallet::InitializeDefaultKey()
{
    CPubKey newDefaultKey;
    if (GetKeyFromPool(newDefaultKey, false))
    {
        LoadDefaultKey(newDefaultKey,true);
        if (!SetAddressLabel(vchDefaultKey.GetID(), "")) {
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
    std::vector<const CWalletTx*> transactions;
    const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
    transactions.reserve(walletTransactionsByHash.size());
    for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.cbegin(); it != walletTransactionsByHash.cend(); ++it)
    {
        transactions.push_back( &(it->second) );
    }
    return transactions;
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
        if (!LoadCryptedHDChain(hdChainCurrent,false))
            throw std::runtime_error(std::string(__func__) + ": LoadCryptedHDChain failed");
    }
    else {
        if (!LoadHDChain(hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": LoadHDChain failed");
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

    return !fFileBacked || GetDatabaseBackend()->WriteHDPubKey(hdPubKey, mapKeyMetadata[extPubKey.pubkey.GetID()]);
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

    UpdateTimeFirstKey(1);
    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return !fFileBacked || GetDatabaseBackend()->WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

void CWallet::ReserializeTransactions(const std::vector<uint256>& transactionIDs)
{
    auto walletDB = GetDatabaseBackend();
    for (uint256 hash: transactionIDs)
    {
        const CWalletTx* txPtr = GetWalletTx(hash);
        if(!txPtr)
        {
            LogPrintf("Trying to write unknown transaction to database!\nHash: %s", hash);
        }
        else if(fFileBacked)
        {
            walletDB->WriteTx(hash, *txPtr );
        }
    }
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

    return !fFileBacked || GetDatabaseBackend()->WriteCScript(Hash160(redeemScript), redeemScript);
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
        return vaultManager_->Sync();
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

    return !fFileBacked || GetDatabaseBackend()->WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);

    return !fFileBacked || GetDatabaseBackend()->EraseWatchOnly(dest);
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

    return !fFileBacked || GetDatabaseBackend()->WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveMultiSig(dest))
        return false;
    if (!HaveMultiSig())
        NotifyMultiSigChanged(false);

    return !fFileBacked || GetDatabaseBackend()->EraseMultiSig(dest);
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
                if(fFileBacked) GetDatabaseBackend()->WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();

                return true;
            }
        }
    }

    return false;
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, bool fExplicit)
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

    if (nWalletVersion > 40000 && !fExplicit)
    {
        if(fFileBacked) GetDatabaseBackend()->WriteMinVersion(nWalletVersion);
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
    return outputTracker_->IsSpent(wtx.GetHash(), n,0);
}
bool CWallet::CanBePruned(const CWalletTx& wtx, const std::set<uint256>& unprunedTransactionIds, const int minimumNumberOfConfs) const
{
    for(unsigned outputIndex = 0; outputIndex < wtx.vout.size(); ++outputIndex)
    {
        if(IsMine(wtx.vout[outputIndex]) != isminetype::ISMINE_NO)
        {
            if(!outputTracker_->IsSpent(wtx.GetHash(), outputIndex,minimumNumberOfConfs)) return false;
        }
    }
    for(const CTxIn& input: wtx.vin)
    {
        const CWalletTx* previousTx = transactionRecord_->GetWalletTx(input.prevout.hash);
        if(previousTx != nullptr &&
            IsMine(previousTx->vout[input.prevout.n]) != isminetype::ISMINE_NO &&
            unprunedTransactionIds.count(input.prevout.hash) > 0)
        {
            return false;
        }
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
        {
            std::unique_ptr<I_WalletDatabase> pwalletdbEncryption = fFileBacked? GetDatabaseBackend() : nullptr;
            CHDChain hdChainCurrent;
            CHDChain hdChainCrypted;
            bool encryptionComplete = !pwalletdbEncryption || pwalletdbEncryption->AtomicWriteBegin();
            try{
                if(encryptionComplete)
                {
                    GetHDChain(hdChainCurrent);
                    encryptionComplete =
                        EncryptKeys(vMasterKey) &&
                        (hdChainCurrent.IsNull() ||
                            (EncryptHDChain(vMasterKey) &&
                            GetHDChain(hdChainCrypted) &&
                            hdChainCurrent.GetID() == hdChainCrypted.GetID() &&
                            hdChainCurrent.GetSeedHash() != hdChainCrypted.GetSeedHash() &&
                            SetCryptedHDChain(hdChainCrypted)) ) &&
                        SetMinVersion(FEATURE_WALLETCRYPT, true);

                    if(encryptionComplete && pwalletdbEncryption)
                    {
                        encryptionComplete =
                            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey) &&
                            (hdChainCurrent.IsNull() || pwalletdbEncryption->WriteCryptedHDChain(hdChainCrypted)) &&
                            pwalletdbEncryption->WriteMinVersion(nWalletVersion);
                    }
                }

            }
            catch(...)
            {
                encryptionComplete = false;
            }
            if(pwalletdbEncryption) pwalletdbEncryption->AtomicWriteEnd(encryptionComplete);
            pwalletdbEncryption.reset();
            assert(encryptionComplete);
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
        if(fFileBacked) GetDatabaseBackend()->RewriteWallet();
    }

    return true;
}

CWallet::TxItems CWallet::OrderedTxItems() const
{
    AssertLockHeld(cs_wallet); // mapWallet
    // First: get all CWalletTx into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
    for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.begin(); it != walletTransactionsByHash.end(); ++it)
    {
        const CWalletTx* wtx = &((*it).second);
        txOrdered.insert(std::make_pair(wtx->nOrderPos, wtx));
        assert(wtx->GetHash()==(*it).first);
    }
    return txOrdered;
}

int64_t CWallet::SmartWalletTxTimestampEstimation(const CWalletTx& wtx)
{
    if(wtx.hashBlock == 0) return wtx.nTimeReceived;
    if(blockIndexByHash_.count(wtx.hashBlock) == 0)
    {
        LogPrintf("%s - found %s in block %s not in index\n",__func__, wtx.ToStringShort(), wtx.hashBlock);
        return wtx.nTimeReceived;
    }
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

static bool topologicallySortTransactions(
    std::vector<CWalletTx>& allTransactions,
    std::map<uint256,std::set<uint256>>& spentTxidsBySpendingTxid,
    std::map<uint256,std::stack<CWalletTx>>& spendingTxBySpentTxid)
{
    std::vector<CWalletTx> sortedTransactions;
    sortedTransactions.reserve(allTransactions.size());
    std::stack<CWalletTx> coinbaseTransactions;
    for(const CWalletTx& walletTx: allTransactions)
    {
        if(walletTx.IsCoinBase()) coinbaseTransactions.push(walletTx);
    }
    while(!coinbaseTransactions.empty())
    {
        CWalletTx coinbaseTx = coinbaseTransactions.top();
        coinbaseTransactions.pop();
        sortedTransactions.push_back(coinbaseTx);
        if(spendingTxBySpentTxid.count(coinbaseTx.GetHash()) > 0)
        {//Has spending txs
            auto& spendingTransactions = spendingTxBySpentTxid[coinbaseTx.GetHash()];
            while(!spendingTransactions.empty())
            {
                CWalletTx spendingTx = spendingTransactions.top();
                spendingTransactions.pop();

                auto& spentTxids = spentTxidsBySpendingTxid[spendingTx.GetHash()];
                spentTxids.erase(coinbaseTx.GetHash());
                if(spentTxids.empty())
                {
                    spentTxidsBySpendingTxid.erase(spendingTx.GetHash());
                    coinbaseTransactions.push(spendingTx);
                }
            }
            spendingTxBySpentTxid.erase(coinbaseTx.GetHash());
        }
    }
    //Verify topological sort
    if(allTransactions.size() != sortedTransactions.size())
    {
        LogPrintf("Wrong tx count!");
        return false;
    }
    std::set<uint256> allTxids;
    for(const CWalletTx& walletTx: allTransactions)
    {
        allTxids.insert(walletTx.GetHash());
    }
    for(unsigned transactionIndex = 0; transactionIndex < sortedTransactions.size(); ++transactionIndex)
    {
        const CWalletTx& tx = sortedTransactions[transactionIndex];
        if(tx.IsCoinBase()) continue;

        auto startOfLookup = sortedTransactions.begin();
        auto endOfLookup = sortedTransactions.begin() + transactionIndex;
        for(const CTxIn& input: tx.vin)
        {
            const uint256 hashToLookup = input.prevout.hash;
            if(allTxids.count(hashToLookup)==0) continue;
            auto it = std::find_if(startOfLookup,endOfLookup,[hashToLookup](const CWalletTx& walletTx)->bool{ return walletTx.GetHash()==hashToLookup;});
            if(it==endOfLookup)
            {
                LogPrintf("tx not found!");
                return false;
            }
        }
    }
    allTransactions = sortedTransactions;
    return true;
}

bool CWallet::PruneWallet()
{
    LOCK2(cs_main,cs_wallet);
    constexpr int64_t defaultMinimumNumberOfConfs = 20;
    const int64_t minimumNumberOfConfs = std::max(settings.GetArg("-prunewalletconfs", defaultMinimumNumberOfConfs),defaultMinimumNumberOfConfs);
    const unsigned totalTxs = transactionRecord_->size();
    std::vector<CWalletTx> transactionsToKeep;
    transactionsToKeep.reserve(1024);

    std::map<uint256,std::stack<CWalletTx>> spendingTxBySpentTxid;
    std::map<uint256,std::set<uint256>> spentTxidsBySpendingTxid;
    std::vector<CWalletTx> allTransactions;
    allTransactions.reserve(totalTxs);
    for(const auto& wtxByHash: transactionRecord_->GetWalletTransactions())
    {
        const CWalletTx& walletTx = wtxByHash.second;
        for(const CTxIn& input: walletTx.vin)
        {
            spendingTxBySpentTxid[input.prevout.hash].push(walletTx);
            spentTxidsBySpendingTxid[wtxByHash.first].insert(input.prevout.hash);
        }
        allTransactions.push_back(walletTx);
    }
    if(!topologicallySortTransactions(allTransactions,spentTxidsBySpendingTxid,spendingTxBySpentTxid))
    {
        LogPrintf("Failed to sort topologically!");
    }

    std::set<uint256> notFullySpent;
    for(const auto& walletTx: allTransactions)
    {
        if(!CanBePruned(walletTx,notFullySpent,minimumNumberOfConfs))
        {
            transactionsToKeep.push_back(walletTx);
            notFullySpent.insert(walletTx.GetHash());
        }
    }
    outputTracker_.reset();
    transactionRecord_.reset();

    transactionRecord_.reset(new PrunedWalletTransactionRecord(cs_wallet,totalTxs));
    outputTracker_.reset( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) );
    for(const CWalletTx& reloadedTransaction: transactionsToKeep)
    {
        LoadWalletTransaction(reloadedTransaction);
    }
    settings.ForceRemoveArg("-prunewalletconfs");
    LogPrintf("Pruned wallet transactions loaded to memory: Original %u vs. Current %u\n",totalTxs,transactionsToKeep.size());
    return true;
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn,bool blockDisconnection)
{
    LOCK(cs_wallet);
    // Inserts only if not already there, returns tx inserted or tx found
    std::pair<CWalletTx*, bool> walletTxAndRecordStatus = outputTracker_->UpdateSpends(wtxIn,false);
    CWalletTx& wtx = *walletTxAndRecordStatus.first;
    wtx.RecomputeCachedQuantities();
    bool transactionHashIsNewToWallet = walletTxAndRecordStatus.second;

    bool walletTransactionHasBeenUpdated = false;
    if (transactionHashIsNewToWallet)
    {
        wtx.nTimeSmart = SmartWalletTxTimestampEstimation(wtx);
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
        if(fFileBacked && !GetDatabaseBackend()->WriteTx(wtx.GetHash(),wtx))
        {
            LogPrintf("%s - Unable to write tx (%s) to disk\n",__func__,wtxIn.ToStringShort());
            return false;
        }
    }

    // Break debit/credit balance caches:
    wtx.RecomputeCachedQuantities();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(wtxIn.GetHash(), (transactionHashIsNewToWallet) ? TransactionNotificationType::NEW : TransactionNotificationType::UPDATED);
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

void CWallet::AddTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);
    if(vaultManager_) vaultManager_->syncTransactions(txs,pblock);
    for(const CTransaction& tx: txs)
    {
        if (!AddToWalletIfInvolvingMe(tx, pblock, true,syncType))
            continue; // Not one of ours

        // If a transaction changes 'conflicted' state, that changes the balance
        // available of the outputs it spends. So force those to be
        // recomputed, also:
        BOOST_FOREACH (const CTxIn& txin, tx.vin)
        {
            CWalletTx* wtx = const_cast<CWalletTx*>(GetWalletTx(txin.prevout.hash));
            if (wtx != nullptr)
                wtx->RecomputeCachedQuantities();
        }
    }
}

void CWallet::SyncTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType)
{
    AssertLockHeld(cs_main);
    if(syncType == TransactionSyncType::RESCAN)
    {
        AssertLockHeld(cs_wallet);
        AddTransactions(txs,pblock,syncType);
    }
    else
    {
        LOCK(cs_wallet);
        AddTransactions(txs,pblock,syncType);
    }
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    if(fFileBacked) GetDatabaseBackend()->WriteBestBlock(loc);
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
    return GetBalanceByCoinType(AvailableCoinsType::STAKABLE_COINS);
}

CAmount CWallet::GetSpendableBalance() const
{
    return GetBalanceByCoinType(AvailableCoinsType::ALL_SPENDABLE_COINS);
}

CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
        for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.begin(); it != walletTransactionsByHash.end(); ++it) {
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
        const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
        for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.begin(); it != walletTransactionsByHash.end(); ++it) {
            const CWalletTx* pcoin = &(*it).second;
            if (IsTrusted(*pcoin))
            {
                int coinTypeEncoding = static_cast<int>(coinType) << 4;
                int additionalFilterFlags = REQUIRE_UNSPENT | REQUIRE_AVAILABLE_TYPE | coinTypeEncoding;
                if(coinType==AvailableCoinsType::STAKABLE_COINS) additionalFilterFlags |= REQUIRE_UNLOCKED;
                nTotal += ComputeCredit(*pcoin,isminetype::ISMINE_SPENDABLE, additionalFilterFlags);
            }

        }

        if(coinType == AvailableCoinsType::STAKABLE_COINS && vaultManager_)
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
        const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
        for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.begin(); it != walletTransactionsByHash.end(); ++it) {
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
        const auto& walletTransactionsByHash = transactionRecord_->GetWalletTransactions();
        for (std::map<uint256, CWalletTx>::const_iterator it = walletTransactionsByHash.begin(); it != walletTransactionsByHash.end(); ++it) {
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
    if (!fOnlyConfirmed && !IsFinalTx(*pcoin, activeChain_, activeChain_.Height() + 1, GetAdjustedTime()))
        return false;
    if (fOnlyConfirmed && !IsFinalTx(*pcoin, activeChain_))
        return false;

    const auto& walletTransaction = *pcoin;
    nDepth = confirmationNumberCalculator_.GetNumberOfBlockConfirmations(walletTransaction);
    if(fOnlyConfirmed && nDepth < 1)
    {
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
    }

    if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && confirmationNumberCalculator_.GetBlocksToMaturity(*pcoin) > 0)
        return false;

    // We should not consider coins which aren't at least in our mempool
    // It's possible for these to be conflicted via ancestors which we may never be able to detect
    if (nDepth == -1) return false;

    return true;
}

bool CWallet::IsAvailableForSpending(
    const CWalletTx* pcoin,
    unsigned int i,
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
    if (pcoin->vout[i].nValue <= 0)
        return false;

    fIsSpendable = (mine == isminetype::ISMINE_SPENDABLE);
    return true;
}
void CWallet::AvailableCoins(
    std::vector<COutput>& vCoins,
    bool fOnlyConfirmed,
    AvailableCoinsType nCoinType,
    CAmount nExactValue) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : transactionRecord_->GetWalletTransactions())
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
                if(!IsAvailableForSpending(pcoin,i,fIsSpendable,nCoinType))
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

bool CWallet::SelectStakeCoins(std::set<StakableCoin>& setCoins) const
{
    CAmount nTargetAmount = GetStakingBalance();
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true,  AvailableCoinsType::STAKABLE_COINS);
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
    AvailableCoins(vCoins, true,  AvailableCoinsType::STAKABLE_COINS);

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

CTxOut CreateChangeOutput(
    const CAmount totalInputs,
    const CAmount totalOutputsPlusFees,
    CReserveKey& reservekey)
{
    CPubKey vchPubKey;
    assert(reservekey.GetReservedKey(vchPubKey, true)); // should never fail, as we just unlocked
    CTxOut changeOutput(totalInputs - totalOutputsPlusFees, GetScriptForDestination(vchPubKey.GetID()));
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
    CMutableTransaction& txNew,
    CAmount& changeAmountTotal)
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

static bool SweepInputsAndTakeFeesFromOutputs(
    const CAmount feesToBePaid,
    const CAmount totalInputs,
    CMutableTransaction& txNew)
{
    if(txNew.vout.size() != 1u) return false;
    txNew.vout[0].nValue = totalInputs;
    CAmount discardedChangeValueAsFees = priorityFeeCalculator.MinimumValueForNonDust();
    return SubtractFeesFromOutputs(feesToBePaid,txNew,discardedChangeValueAsFees);
}

enum ChangeUseStatus
{
    USE_CHANGE_OUTPUT,
    ROLLED_CHANGE_INTO_FEES,
};

static ChangeUseStatus SetChangeOutput(
    CMutableTransaction& txNew,
    CAmount& nFeeRet,
    CTxOut& changeOutput)
{
    bool changeOutputShouldBeUsed = changeOutput.nValue > 0;
    if (changeOutputShouldBeUsed && priorityFeeCalculator.IsDust(changeOutput))
    {
        nFeeRet += changeOutput.nValue;
        changeOutput.nValue = 0;
        changeOutputShouldBeUsed = false;
    }
    if(changeOutputShouldBeUsed)
    {
        AttachChangeOutput(changeOutput,txNew);
    }
    return changeOutputShouldBeUsed? ChangeUseStatus::USE_CHANGE_OUTPUT: ChangeUseStatus::ROLLED_CHANGE_INTO_FEES;
}

class SweepFundsCoinSelectionAlgorithm final: public I_CoinSelectionAlgorithm
{
private:
    const I_CoinSelectionAlgorithm& wrappedAlgorithm_;
public:
    SweepFundsCoinSelectionAlgorithm(
        const I_CoinSelectionAlgorithm& wrappedAlgorithm
        ): wrappedAlgorithm_(wrappedAlgorithm)
    {
    }
    bool isSelectable(const COutput& coin) const override
    {
        return wrappedAlgorithm_.isSelectable(coin);
    }

    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override
    {
        CMutableTransaction txCopy = transactionToSelectCoinsFor;
        std::set<COutput> selectedCoins;
        std::copy_if(
            vCoins.begin(),vCoins.end(),
            std::inserter(selectedCoins,selectedCoins.begin()),
            [this](const COutput coin){ return wrappedAlgorithm_.isSelectable(coin);});
        CAmount nValueIn = AttachInputs(selectedCoins,txCopy);
        txCopy.vout[0].nValue = nValueIn;
        wrappedAlgorithm_.SelectCoins(txCopy,vCoins,fees);
        return selectedCoins;
    }
};

static std::pair<std::string,bool> SelectInputsProvideSignaturesAndFees(
    const CKeyStore& walletKeyStore,
    const I_CoinSelectionAlgorithm* coinSelector,
    const std::vector<COutput>& vCoins,
    const TransactionFeeMode sendMode,
    CMutableTransaction& txNew,
    CReserveKey& reservekey,
    CWalletTx& wtxNew)
{
    const CAmount totalValueToSend = txNew.GetValueOut();
    CAmount nFeeRet = 0;
    if(sendMode != TransactionFeeMode::SWEEP_FUNDS && !(totalValueToSend > 0))
    {
        return {translate("Transaction amounts must be positive. Total output may not exceed limits."),false};
    }
    txNew.vin.clear();
    // Choose coins to use
    const bool sweepMode = sendMode == TransactionFeeMode::SWEEP_FUNDS;
    const std::set<COutput> setCoins =
        (sweepMode)
        ? SweepFundsCoinSelectionAlgorithm(*coinSelector).SelectCoins(txNew,vCoins,nFeeRet)
        : coinSelector->SelectCoins(txNew,vCoins,nFeeRet);
    const CAmount nValueIn = AttachInputs(setCoins,txNew);

    const CAmount totalValueToSendPlusFees = (!sweepMode)? totalValueToSend + nFeeRet: nValueIn;
    if (setCoins.empty() || nValueIn < totalValueToSendPlusFees)
    {
        return {translate("Insufficient funds to meet coin selection algorithm requirements."),false};
    }
    else if(sweepMode && nValueIn < nFeeRet)
    {
        return {translate("Insufficient funds to to pay for tx fee for the requested coin selection algorithm."),false};
    }

    CTxOut changeOutput = CreateChangeOutput(nValueIn,totalValueToSendPlusFees,reservekey);
    switch (sendMode)
    {
    case TransactionFeeMode::RECEIVER_PAYS_FOR_TX_FEES:
        if(!SubtractFeesFromOutputs(nFeeRet,txNew,changeOutput.nValue))
        {
            return {translate("Cannot subtract needed fees from outputs."),false};
        }
        break;
    case TransactionFeeMode::SWEEP_FUNDS:
        if(!SweepInputsAndTakeFeesFromOutputs(nFeeRet,nValueIn,txNew))
        {
            return {translate("Cannot sweep inputs."),false};
        }
        break;
    default:
        break;
    }

    bool changeUsed = SetChangeOutput(txNew,nFeeRet,changeOutput) == ChangeUseStatus::USE_CHANGE_OUTPUT;
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
        if(!changeUsed)
        {
            reservekey.ReturnKey();
        }
        return {std::string(""),true};
    }
    return {translate("Selected too few inputs to meet fees"),false};
}

std::pair<std::string,bool> CWallet::CreateTransaction(
    const std::vector<std::pair<CScript, CAmount> >& vecSend,
    const TransactionFeeMode feeMode,
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
    AvailableCoins(vCoins, true,  coin_type);
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
    return SelectInputsProvideSignaturesAndFees(*this, coinSelector,vCoins,feeMode,txNew,reservekey,wtxNew);
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
            const auto walletDatabase = fFileBacked? GetDatabaseBackend(): std::unique_ptr<I_WalletDatabase>();

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
        }
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
            requestedTransaction.transactionFeeMode,
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
        InitializeDatabaseBackend();
        nLoadWalletRet = GetDatabaseBackend()->LoadWallet(*static_cast<I_WalletLoader*>(this));
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
    if(nLoadWalletRet == DB_LOAD_OK_FIRST_RUN)
    {
        if(!settings.ParameterIsSet("-hdseed") && !settings.ParameterIsSet("-mnemonic"))
        {
            LogPrintf("%s -- Setting the best chain for wallet to the active chain...\n",__func__);
            SetBestChain(activeChain_.GetLocator());
        }
    }
    return nLoadWalletRet;
}

bool CWallet::SetAddressLabel(const CTxDestination& address, const std::string& strName)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet);
        fUpdated = addressBookManager_->SetAddressLabel(address,strName);
    }
    NotifyAddressBookChanged(address, strName, ::IsMine(*this, address) != isminetype::ISMINE_NO, (fUpdated ? "updated address label" : "new address label"));

    return !fFileBacked || GetDatabaseBackend()->WriteName(CBitcoinAddress(address).ToString(), strName);
}

const AddressBookManager& CWallet::GetAddressBookManager() const
{
    return *addressBookManager_;
}

void CWallet::LoadAddressLabel(const CTxDestination& address, const std::string newLabel)
{
    addressBookManager_->SetAddressLabel(address,newLabel);
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        auto walletdb = GetDatabaseBackend();
        BOOST_FOREACH(int64_t nIndex, setInternalKeyPool) {
            if(walletdb) walletdb->ErasePool(nIndex);
        }
        setInternalKeyPool.clear();
        BOOST_FOREACH(int64_t nIndex, setExternalKeyPool) {
            if(walletdb) walletdb->ErasePool(nIndex);
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
        auto walletdb = GetDatabaseBackend();
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
            if (walletdb && !walletdb->WritePool(nEnd, CKeyPool(GenerateNewKey(0, fInternal), fInternal)))
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

        auto walletdb = GetDatabaseBackend();

        nIndex = *setKeyPool.begin();
        setKeyPool.erase(nIndex);
        if (walletdb && !walletdb->ReadPool(nIndex, keypool)) {
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
        GetDatabaseBackend()->ErasePool(nIndex);
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
    bool hdchainIsFromSeedRestore = false;
    if(settings.ParameterIsSet("-hdseed") && IsHex(strSeed)) {
        std::vector<unsigned char> vchSeed = ParseHex(strSeed);
        if (!newHdChain.SetSeed(SecureVector(vchSeed.begin(), vchSeed.end()), true))
            throw std::runtime_error(std::string(__func__) + ": SetSeed failed");

        hdchainIsFromSeedRestore = true;
    }
    else {
        if (settings.ParameterIsSet("-hdseed") && !IsHex(strSeed))
            LogPrintf("CWallet::GenerateNewHDChain -- Incorrect seed, generating random one instead\n");

        // NOTE: empty mnemonic means "generate a new one for me"
        std::string strMnemonic = settings.GetArg("-mnemonic", "");
        if(strMnemonic.empty()) LogPrintf("Generating new seed for wallet...!\n");
        // NOTE: default mnemonic passphrase is an empty string
        std::string strMnemonicPassphrase = settings.GetArg("-mnemonicpassphrase", "");

        SecureVector vchMnemonic(strMnemonic.begin(), strMnemonic.end());
        SecureVector vchMnemonicPassphrase(strMnemonicPassphrase.begin(), strMnemonicPassphrase.end());

        if (!newHdChain.SetMnemonic(vchMnemonic, vchMnemonicPassphrase, true))
            throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed");

        hdchainIsFromSeedRestore = !strMnemonic.empty();
    }

    if (!LoadHDChain(newHdChain, false))
        throw std::runtime_error(std::string(__func__) + ": LoadHDChain failed");

    // clean up
    settings.ForceRemoveArg("-hdseed");
    settings.ForceRemoveArg("-mnemonic");
    settings.ForceRemoveArg("-mnemonicpassphrase");

    if(hdchainIsFromSeedRestore)
    {
        LogPrintf("Requesting rescan due to seed restore...\n");
        settings.SetParameter("-force_rescan","1");
        assert(settings.GetBoolArg("-force_rescan",false));
        settings.SetParameter("-rescan","1");
        assert(settings.GetBoolArg("-rescan",false));
    }
}

bool CWallet::LoadHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetHDChain(chain))
        return false;

    if (!memonly && fFileBacked && !GetDatabaseBackend()->WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": WriteHDChain failed");

    return true;
}

bool CWallet::LoadCryptedHDChain(const CHDChain& chain, bool memonly)
{
    AssertLockHeld(cs_wallet);

    if (!CCryptoKeyStore::SetCryptedHDChain(chain))
        return false;

    if (!memonly) {
        if (fFileBacked && !GetDatabaseBackend()->WriteCryptedHDChain(chain))
            throw std::runtime_error(std::string(__func__) + ": WriteCryptedHDChain failed");
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
