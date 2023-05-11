// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"

#include <walletdb.h>
#include <primitives/transaction.h>

#include <clientversion.h>
#include <dbenv.h>
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
#include <BlockScanner.h>
#include <ui_interface.h>
#include <UtxoBalanceCalculator.h>
#include <WalletBalanceCalculator.h>
#include <CachedUtxoBalanceCalculator.h>
#include <script/StakingVaultScript.h>
#include <I_WalletDatabaseEndpointFactory.h>
#include <AvailableUtxoCollector.h>
#include <CachedTransactionDeltasCalculator.h>

#include <stack>

#include "Settings.h"
extern Settings& settings;

const FeeAndPriorityCalculator& priorityFeeCalculator = FeeAndPriorityCalculator::instance();

extern CCriticalSection cs_main;

template <typename T>
isminetype computeMineType(const CKeyStore& keystore, const T& destinationOrScript, const bool parseVaultsAsSpendable)
{
    isminetype mine = ::IsMine(keystore, destinationOrScript);
    const bool isManagedVault = mine == isminetype::ISMINE_MANAGED_VAULT;
    const bool isOwnedVault = mine == isminetype::ISMINE_OWNED_VAULT;
    const bool isVault = isManagedVault || isOwnedVault;
    if(isVault && parseVaultsAsSpendable) mine = isminetype::ISMINE_SPENDABLE;
    return mine;
}

template isminetype computeMineType<CTxDestination>(const CKeyStore& keystore, const CTxDestination& destinationOrScript, const bool parseVaultsAsSpendable);
template isminetype computeMineType<CScript>(const CKeyStore& keystore, const CScript& destinationOrScript, const bool parseVaultsAsSpendable);

TransactionCreationRequest::TransactionCreationRequest(
    const std::vector<std::pair<CScript, CAmount> >& scriptsToSendTo,
    const CScript& changeAddressOverride,
    TransactionFeeMode txFeeMode,
    TxTextMetadata metadataToSet,
    AvailableCoinsType coinTypeSelected,
    const I_CoinSelectionAlgorithm& algorithm
    ): scriptsToFund(scriptsToSendTo)
    , changeAddress(changeAddressOverride)
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
{
}
TransactionCreationResult::~TransactionCreationResult()
{
    wtxNew.reset();
}
TransactionCreationResult::TransactionCreationResult(TransactionCreationResult&& other)
{
    transactionCreationSucceeded = other.transactionCreationSucceeded;
    errorMessage = other.errorMessage;
    wtxNew.reset(other.wtxNew.release());
}

class WalletUtxoOwnershipDetector final: public I_UtxoOwnershipDetector
{
private:
    const CKeyStore& keyStore_;
    const std::map<CKeyID, CHDPubKey>& hdPubKeysByKeyID_;

public:
    WalletUtxoOwnershipDetector(
        const CKeyStore& keyStore,
        const std::map<CKeyID, CHDPubKey>& hdPubKeysByKeyID
        ): keyStore_(keyStore)
        , hdPubKeysByKeyID_(hdPubKeysByKeyID)
    {
    }

    isminetype isMine(const CTxOut& output) const override
    {
        return computeMineType(keyStore_,output.scriptPubKey,false);
    }

    bool isChange(const CTxOut& output) const override
    {
        if (isMine(output) != isminetype::ISMINE_NO)
        {
            CTxDestination address;
            if (!ExtractDestination(output.scriptPubKey, address))
                return true;

            if(output.scriptPubKey.IsPayToPublicKeyHash())
            {
                auto keyID = boost::get<CKeyID>(address);
                auto it = hdPubKeysByKeyID_.find(keyID);
                if(it != hdPubKeysByKeyID_.end()) return it->second.nChangeIndex > 0;
            }
        }
        return false;
    }
};

CWallet::CWallet(
    CCriticalSection& walletCriticalSection,
    const I_WalletDatabaseEndpointFactory& walletDatabaseEndpointFactory,
    const CChain& chain,
    const BlockMap& blockMap,
    const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator,
    const unsigned defaultKeyTopUp
    ): cs_wallet(walletCriticalSection)
    , vaultModeEnabled_(settings.GetBoolArg("-vault", false))
    , setLockedCoins()
    , mapHdPubKeys()
    , walletDatabaseEndpointFactory_(walletDatabaseEndpointFactory)
    , activeChain_(chain)
    , blockIndexByHash_(blockMap)
    , confirmationNumberCalculator_(confirmationNumberCalculator)
    , addressBookManager_(new AddressBookManager())
    , transactionRecord_(new WalletTransactionRecord(cs_wallet) )
    , outputTracker_( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) )
    , ownershipDetector_(new WalletUtxoOwnershipDetector(*static_cast<CKeyStore*>(this), mapHdPubKeys))
    , availableUtxoCollector_(
        new AvailableUtxoCollector(
            settings,
            blockIndexByHash_,
            activeChain_,
            *transactionRecord_,
            confirmationNumberCalculator_,
            *ownershipDetector_,
            *outputTracker_,
            setLockedCoins,
            cs_main))
    , utxoBalanceCalculator_( new UtxoBalanceCalculator(*ownershipDetector_,*outputTracker_) )
    , cachedUtxoBalanceCalculator_( new CachedUtxoBalanceCalculator(*utxoBalanceCalculator_) )
    , balanceCalculator_(
        new WalletBalanceCalculator(
            *ownershipDetector_,
            *cachedUtxoBalanceCalculator_,
            *transactionRecord_,
            confirmationNumberCalculator_  ))
    , cachedTxDeltasCalculator_(new CachedTransactionDeltasCalculator(*ownershipDetector_, *transactionRecord_, Params().MaxMoneyOut()))
    , nWalletVersion(FEATURE_BASE)
    , nWalletMaxVersion(FEATURE_BASE)
    , mapKeyMetadata()
    , mapMasterKeys()
    , nMasterKeyMaxID(0)
    , vchDefaultKey()
    , nTimeFirstKey(0)
    , setInternalKeyPool()
    , setExternalKeyPool()
    , walletStakingOnly(false)
    , defaultKeyPoolTopUp_(defaultKeyTopUp)
{
}

CWallet::~CWallet()
{
    balanceCalculator_.reset();
    cachedUtxoBalanceCalculator_.reset();
    utxoBalanceCalculator_.reset();
    availableUtxoCollector_.reset();
    ownershipDetector_.reset();
    outputTracker_.reset();
    transactionRecord_.reset();
    addressBookManager_.reset();
}

CCriticalSection& CWallet::getWalletCriticalSection() const
{
    return cs_wallet;
}

int64_t CWallet::getTimestampOfFistKey() const
{
    return nTimeFirstKey;
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

const CBlockIndex* CWallet::getNextUnsycnedBlockIndexInMainChain(bool syncFromGenesis)
{
    CBlockLocator locator;
    const bool forceSyncFromGenesis = settings.GetBoolArg("-force_rescan",false);
    if( forceSyncFromGenesis || (!syncFromGenesis && !walletDatabaseEndpointFactory_.getDatabaseEndpoint()->ReadBestBlock(locator) ) ) syncFromGenesis = true;
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
    const CBlockIndex* const startingBlockIndex = getNextUnsycnedBlockIndexInMainChain(startFromGenesis);
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

bool CWallet::loadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey)
{
    if (mapMasterKeys.count(masterKeyIndex) != 0) {
        return false;
    }
    mapMasterKeys[masterKeyIndex] = masterKey;
    if (nMasterKeyMaxID < masterKeyIndex)
        nMasterKeyMaxID = masterKeyIndex;

    return true;
}

bool CWallet::loadKey(const CKey& key, const CPubKey& pubkey)
{
    return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
}

bool CWallet::loadDefaultKey(const CPubKey& vchPubKey, bool updateDatabase)
{
    if (updateDatabase) {
        if (!walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

bool CWallet::initializeDefaultKey()
{
    CPubKey newDefaultKey;
    if (GetKeyFromPool(newDefaultKey, false))
    {
        loadDefaultKey(newDefaultKey,true);
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

bool CWallet::canSupportFeature(enum WalletFeature wf)
{
    AssertLockHeld(cs_wallet);
    return nWalletMaxVersion >= wf;
}

CAmount CWallet::getDebit(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& filter) const
{
    if (walletTransaction.vin.empty())
        return 0;

    CachedTransactionDeltas txDeltas;
    cachedTxDeltasCalculator_->calculate(walletTransaction,filter,txDeltas);
    return txDeltas.debit;
}
CAmount CWallet::getCredit(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& filter) const
{
    CachedTransactionDeltas txDeltas;
    cachedTxDeltasCalculator_->calculate(walletTransaction,filter,txDeltas);
    return txDeltas.credit;
}

CAmount CWallet::getChange(const CWalletTx& walletTransaction) const
{
    CachedTransactionDeltas txDeltas;
    cachedTxDeltasCalculator_->calculate(walletTransaction,isminetype::ISMINE_SPENDABLE,txDeltas);
    return txDeltas.changeAmount;
}

int CWallet::getVersion()
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
    bool fCompressed = canSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    CPubKey pubkey;
    // use HD key derivation if HD was enabled during wallet creation
    if (IsHDEnabled()) {
        deriveNewChildKey(metadata, secret, nAccountIndex, fInternal);
        pubkey = secret.GetPubKey();
    } else {
        secret.MakeNewKey(fCompressed);

        // Compressed public keys were introduced in version 0.6.0
        if (fCompressed)
            setMinVersion(FEATURE_COMPRPUBKEY);

        pubkey = secret.GetPubKey();
        assert(secret.VerifyPubKey(pubkey));

        // Create new metadata
        mapKeyMetadata[pubkey.GetID()] = metadata;
        updateTimeFirstKey(nCreationTime);

        if (!AddKeyPubKey(secret, pubkey))
            throw std::runtime_error(std::string(__func__) + ": AddKey failed");
    }
    return pubkey;
}

void CWallet::deriveNewChildKey(const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal)
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
    updateTimeFirstKey(metadata.nCreateTime);

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
        if (!loadCryptedHDChain(hdChainCurrent,false))
            throw std::runtime_error(std::string(__func__) + ": loadCryptedHDChain failed");
    }
    else {
        if (!loadHDChain(hdChainCurrent, false))
            throw std::runtime_error(std::string(__func__) + ": loadHDChain failed");
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

bool CWallet::loadHDPubKey(const CHDPubKey &hdPubKey)
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

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteHDPubKey(hdPubKey, mapKeyMetadata[extPubKey.pubkey.GetID()]);
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

    updateTimeFirstKey(1);
    if (!IsCrypted()) {
        return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

void CWallet::reserializeTransactions(const std::vector<uint256>& transactionIDs)
{
    auto walletDB = walletDatabaseEndpointFactory_.getDatabaseEndpoint();
    for (uint256 hash: transactionIDs)
    {
        const CWalletTx* txPtr = GetWalletTx(hash);
        if(!txPtr)
        {
            LogPrintf("Trying to write unknown transaction to database!\nHash: %s", hash);
        }
        else
        {
            walletDB->WriteTx(hash, *txPtr );
        }
    }
}

bool CWallet::loadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& meta, const bool updateFirstKeyTimestamp)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey) && updateFirstKeyTimestamp)
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    if(updateFirstKeyTimestamp) updateTimeFirstKey(meta.nCreateTime);
    return true;
}

bool CWallet::loadMinVersion(int nVersion)
{
    AssertLockHeld(cs_wallet);
    nWalletVersion = nVersion;
    nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);
    return true;
}

void CWallet::updateTimeFirstKey(int64_t nCreateTime)
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

bool CWallet::loadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::loadCScript(const CScript& redeemScript)
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
    if(vaultModeEnabled_)
    {
        LOCK(cs_wallet);
        AddCScript(vaultScript);
        addTransactions({tx},pblock,TransactionSyncType::RESCAN);
        return GetWalletTx(tx.GetHash()) != nullptr;
    }
    return false;
}
bool CWallet::RemoveVault(const CScript& vaultScript)
{
    if(vaultModeEnabled_)
    {
        LOCK2(cs_wallet,cs_KeyStore);
        mapScripts.erase(CScriptID(vaultScript));
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

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->EraseWatchOnly(dest);
}

bool CWallet::loadWatchOnly(const CScript& dest)
{
    // Watch-only addresses have no birthday information for now,
    // so set the wallet birthday to the beginning of time.
    updateTimeFirstKey(1);
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::AddMultiSig(const CScript& dest)
{
    if (!CCryptoKeyStore::AddMultiSig(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information
    NotifyMultiSigChanged(true);

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveMultiSig(dest))
        return false;
    if (!HaveMultiSig())
        NotifyMultiSigChanged(false);

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->EraseMultiSig(dest);
}

bool CWallet::loadMultiSig(const CScript& dest)
{
    // MultiSig addresses have no birthday information for now,
    // so set the wallet birthday to the beginning of time.
    updateTimeFirstKey(1);
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
                walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();

                return true;
            }
        }
    }

    return false;
}

bool CWallet::setMinVersion(enum WalletFeature nVersion, bool fExplicit)
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
        walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteMinVersion(nWalletVersion);
    }

    return true;
}

bool CWallet::setMaxVersion(int nVersion)
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

bool CWallet::canBePruned(const CWalletTx& wtx, const std::set<uint256>& unprunedTransactionIds, const int minimumNumberOfConfs) const
{
    for(unsigned outputIndex = 0; outputIndex < wtx.vout.size(); ++outputIndex)
    {
        if(ownershipDetector_->isMine(wtx.vout[outputIndex]) != isminetype::ISMINE_NO)
        {
            if(!outputTracker_->IsSpent(wtx.GetHash(), outputIndex,minimumNumberOfConfs)) return false;
        }
    }
    for(const CTxIn& input: wtx.vin)
    {
        const CWalletTx* previousTx = transactionRecord_->GetWalletTx(input.prevout.hash);
        if(previousTx != nullptr &&
            ownershipDetector_->isMine(previousTx->vout[input.prevout.n]) != isminetype::ISMINE_NO &&
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
            std::unique_ptr<I_WalletDatabase> pwalletdbEncryption = walletDatabaseEndpointFactory_.getDatabaseEndpoint();
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
                        setMinVersion(FEATURE_WALLETCRYPT, true);

                    if(encryptionComplete && pwalletdbEncryption)
                    {
                        encryptionComplete =
                            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey) &&
                            (hdChainCurrent.IsNull() || pwalletdbEncryption->WriteCryptedHDChain(hdChainCrypted)) &&
                            pwalletdbEncryption->WriteMinVersion(nWalletVersion);

                        if(encryptionComplete)
                        {
                            for(const auto& pubkeyAndEncryptedKey: getCryptedKeys())
                            {
                                const CPubKey & pubKey = pubkeyAndEncryptedKey.second.first;
                                const std::vector<unsigned char> & encryptedKey = pubkeyAndEncryptedKey.second.second;
                                if(!pwalletdbEncryption->WriteCryptedKey(pubKey,encryptedKey,mapKeyMetadata[pubKey.GetID()]))
                                {
                                    encryptionComplete = false;
                                }
                            }
                        }
                    }
                }

            }
            catch(...)
            {
                encryptionComplete = false;
            }
            if(pwalletdbEncryption) pwalletdbEncryption->AtomicWriteEnd(encryptionComplete);
            pwalletdbEncryption.reset();
            if(!encryptionComplete) return false;
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
        if(!walletDatabaseEndpointFactory_.getDatabaseEndpoint()->RewriteWallet())
        {
            return false;
        }
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

int64_t CWallet::smartWalletTxTimestampEstimation(const CWalletTx& wtx)
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

void CWallet::loadWalletTransaction(const CWalletTx& wtxIn)
{
    outputTracker_->UpdateSpends(wtxIn, true);
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
        if(!canBePruned(walletTx,notFullySpent,minimumNumberOfConfs))
        {
            transactionsToKeep.push_back(walletTx);
            notFullySpent.insert(walletTx.GetHash());
        }
    }

    cachedTxDeltasCalculator_.reset();
    balanceCalculator_.reset();
    cachedUtxoBalanceCalculator_.reset();
    utxoBalanceCalculator_.reset();
    availableUtxoCollector_.reset();
    outputTracker_.reset();
    transactionRecord_.reset();

    transactionRecord_.reset(new PrunedWalletTransactionRecord(cs_wallet,totalTxs));
    outputTracker_.reset( new SpentOutputTracker(*transactionRecord_,confirmationNumberCalculator_) );
    availableUtxoCollector_.reset(
        new AvailableUtxoCollector(
            settings,
            blockIndexByHash_,
            activeChain_,
            *transactionRecord_,
            confirmationNumberCalculator_,
            *ownershipDetector_,
            *outputTracker_,
            setLockedCoins,
            cs_main));
    utxoBalanceCalculator_.reset( new UtxoBalanceCalculator(*ownershipDetector_,*outputTracker_) );
    cachedUtxoBalanceCalculator_.reset( new CachedUtxoBalanceCalculator(*utxoBalanceCalculator_) );
    balanceCalculator_.reset(
        new WalletBalanceCalculator(
            *ownershipDetector_,
            *cachedUtxoBalanceCalculator_,
            *transactionRecord_,
            confirmationNumberCalculator_  ));
    cachedTxDeltasCalculator_.reset(new CachedTransactionDeltasCalculator(*ownershipDetector_, *transactionRecord_, Params().MaxMoneyOut()));



    for(const CWalletTx& reloadedTransaction: transactionsToKeep)
    {
        loadWalletTransaction(reloadedTransaction);
    }
    settings.ForceRemoveArg("-prunewalletconfs");
    LogPrintf("Pruned wallet transactions loaded to memory: Original %u vs. Current %u\n",totalTxs,transactionsToKeep.size());
    return true;
}

bool CWallet::addToWallet(const CWalletTx& wtxIn,bool blockDisconnection)
{
    LOCK(cs_wallet);
    // Inserts only if not already there, returns tx inserted or tx found
    std::pair<CWalletTx*, bool> walletTxAndRecordStatus = outputTracker_->UpdateSpends(wtxIn,false);
    CWalletTx& wtx = *walletTxAndRecordStatus.first;
    cachedUtxoBalanceCalculator_->recomputeCachedTxEntries(wtx);
    cachedTxDeltasCalculator_->recomputeCachedTxEntries(wtx);
    bool transactionHashIsNewToWallet = walletTxAndRecordStatus.second;

    bool walletTransactionHasBeenUpdated = false;
    if (transactionHashIsNewToWallet)
    {
        wtx.nTimeSmart = smartWalletTxTimestampEstimation(wtx);
    }
    else
    {
        walletTransactionHasBeenUpdated = wtx.UpdateTransaction(wtxIn,blockDisconnection);
    }

    //// debug print
    const std::string updateDescription =
        transactionHashIsNewToWallet ? "new":
        walletTransactionHasBeenUpdated? "update" : "";
    LogPrintf("addToWallet %s %s\n", wtxIn.ToStringShort(), updateDescription);

    // Write to disk
    if (transactionHashIsNewToWallet || walletTransactionHasBeenUpdated)
    {
        if(!walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteTx(wtx.GetHash(),wtx))
        {
            LogPrintf("%s - Unable to write tx (%s) to disk\n",__func__,wtxIn.ToStringShort());
            return false;
        }
    }

    // Break debit/credit balance caches:
    cachedUtxoBalanceCalculator_->recomputeCachedTxEntries(wtx);
    cachedTxDeltasCalculator_->recomputeCachedTxEntries(wtx);

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(wtxIn.GetHash(), (transactionHashIsNewToWallet) ? TransactionNotificationType::NEW : TransactionNotificationType::UPDATED);
    return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
bool CWallet::addToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, const TransactionSyncType syncType)
{
    {
        AssertLockHeld(cs_wallet);
        bool fExisted = GetWalletTx(tx.GetHash()) != nullptr;
        if (fExisted && !fUpdate) return false;

        CachedTransactionDeltas txDeltas;
        cachedTxDeltasCalculator_->calculate(tx,isminetype::ISMINE_SPENDABLE,txDeltas);
        const bool creditsFundsOrDebitsFunds = txDeltas.credit > 0 || txDeltas.debit > 0;
        cachedTxDeltasCalculator_->recomputeCachedTxEntries(tx);
        if (fExisted || creditsFundsOrDebitsFunds) {
            CWalletTx wtx(tx);
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(*pblock);
            return addToWallet(wtx,syncType == TransactionSyncType::BLOCK_DISCONNECT);
        }
    }
    return false;
}

void CWallet::addTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);
    for(const CTransaction& tx: txs)
    {
        if (!addToWalletIfInvolvingMe(tx, pblock, true,syncType))
            continue; // Not one of ours

        // If a transaction changes 'conflicted' state, that changes the balance
        // available of the outputs it spends. So force those to be
        // recomputed, also:
        BOOST_FOREACH (const CTxIn& txin, tx.vin)
        {
            CWalletTx* wtx = const_cast<CWalletTx*>(GetWalletTx(txin.prevout.hash));
            if (wtx != nullptr)
            {
                cachedUtxoBalanceCalculator_->recomputeCachedTxEntries(*wtx);
                cachedTxDeltasCalculator_->recomputeCachedTxEntries(*wtx);
            }
        }
    }
}

void CWallet::SyncTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType)
{
    AssertLockHeld(cs_main);
    if(syncType == TransactionSyncType::RESCAN)
    {
        AssertLockHeld(cs_wallet);
        addTransactions(txs,pblock,syncType);
    }
    else
    {
        LOCK(cs_wallet);
        addTransactions(txs,pblock,syncType);
    }
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteBestBlock(loc);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    return ownershipDetector_->isChange(txout);
}

/** @} */ // end of mapWallet


/** @defgroup Actions
 *
 * @{
 */
CAmount CWallet::GetVaultedBalance() const
{
    LOCK2(cs_main,cs_wallet);
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_OWNED_VAULT);
    return balanceCalculator_->getBalance(filter);
}
CAmount CWallet::GetStakingBalance() const
{
    LOCK2(cs_main,cs_wallet);
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    filter.addOwnershipType(isminetype::ISMINE_MANAGED_VAULT);
    CAmount totalLockedCoinsBalance = lockedCoinBalance(filter);
    return balanceCalculator_->getBalance(filter) - totalLockedCoinsBalance;
}

CAmount CWallet::GetSpendableBalance() const
{
    LOCK2(cs_main,cs_wallet);
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    return balanceCalculator_->getSpendableBalance(filter);
}

CAmount CWallet::GetBalance() const
{
    LOCK2(cs_main, cs_wallet);
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    filter.addOwnershipType(isminetype::ISMINE_OWNED_VAULT);
    return balanceCalculator_->getBalance(filter);
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        nTotal += balanceCalculator_->getUnconfirmedBalance();
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        nTotal += balanceCalculator_->getImmatureBalance();
    }
    return nTotal;
}

void CWallet::AvailableCoins(
    std::vector<COutput>& vCoins,
    bool fOnlyConfirmed,
    AvailableCoinsType nCoinType) const
{
    LOCK2(cs_main, cs_wallet);
    availableUtxoCollector_->setCoinTypeAndGetAvailableUtxos(fOnlyConfirmed, nCoinType, vCoins);
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

bool CWallet::HasAgedCoins() const
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

class ChangeOutputCreator
{
private:
    CReserveKey* const reserveKey_;
    const CScript* const changeAddress_;
public:
    explicit ChangeOutputCreator(CReserveKey& reserveKey);
    explicit ChangeOutputCreator(const CScript& changeAddress);

    CTxOut createChangeOutput(const CAmount amount) const;
    void reset() const {
        if(reserveKey_) reserveKey_->ReturnKey();
    }
    void commit() const {
        if(reserveKey_) reserveKey_->KeepKey();
    }
};

ChangeOutputCreator::ChangeOutputCreator(
    CReserveKey& reserveKey
    ): reserveKey_(&reserveKey)
    , changeAddress_(nullptr)
{
}

ChangeOutputCreator::ChangeOutputCreator(
    const CScript& changeAddress
    ): reserveKey_(nullptr)
    , changeAddress_(&changeAddress)
{
}

CTxOut ChangeOutputCreator::createChangeOutput(const CAmount amount) const
{
    if(reserveKey_)
    {
        CPubKey vchPubKey;
        assert(reserveKey_->GetReservedKey(vchPubKey, true)); // should never fail, as we just unlocked
        CTxOut changeOutput(amount, GetScriptForDestination(vchPubKey.GetID()));
        return changeOutput;
    }
    else
    {
        CTxOut changeOutput(amount, *changeAddress_);
        return changeOutput;
    }
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
        const std::vector<COutput>& allSpendableCoins,
        CAmount& fees) const override
    {
        CMutableTransaction transactionToEstimateFeesFor = transactionToSelectCoinsFor;
        std::set<COutput> selectedCoins;
        std::copy_if(
            allSpendableCoins.begin(),allSpendableCoins.end(),
            std::inserter(selectedCoins,selectedCoins.begin()),
            [this](const COutput coin){ return wrappedAlgorithm_.isSelectable(coin);});
        CAmount nValueIn = AttachInputs(selectedCoins, transactionToEstimateFeesFor);
        transactionToEstimateFeesFor.vout[0].nValue = nValueIn;
        wrappedAlgorithm_.SelectCoins(transactionToEstimateFeesFor,allSpendableCoins,fees);
        return selectedCoins;
    }
};

static std::pair<std::string,bool> SelectInputsProvideSignaturesAndFees(
    const CKeyStore& walletKeyStore,
    const I_CoinSelectionAlgorithm& coinSelector,
    const std::vector<COutput>& vCoins,
    const TransactionFeeMode sendMode,
    CMutableTransaction& txNew,
    const ChangeOutputCreator& changeOutputCreator,
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
        ? SweepFundsCoinSelectionAlgorithm(coinSelector).SelectCoins(txNew,vCoins,nFeeRet)
        : coinSelector.SelectCoins(txNew,vCoins,nFeeRet);
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

    CTxOut changeOutput = changeOutputCreator.createChangeOutput(nValueIn-totalValueToSendPlusFees);
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
            changeOutputCreator.reset();
        }
        return {std::string(""),true};
    }
    return {translate("Selected too few inputs to meet fees"),false};
}

std::pair<std::string,bool> CWallet::CreateTransaction(
    const TransactionCreationRequest& requestedTransaction,
    const ChangeOutputCreator& changeOutputCreator,
    CWalletTx& wtxNew)
{
    const std::vector<std::pair<CScript, CAmount> >& vecSend =   requestedTransaction.scriptsToFund;
    const TransactionFeeMode feeMode =  requestedTransaction.transactionFeeMode;
    const I_CoinSelectionAlgorithm& coinSelector = requestedTransaction.coinSelectionAlgorithm;
    const AvailableCoinsType coin_type =  requestedTransaction.coin_type;
    if (vecSend.empty())
    {
        return {translate("Must provide at least one destination for funds."),false};
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.createdByMe = true;
    CMutableTransaction txNew;

    LOCK2(cs_main, cs_wallet);
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, !settings.GetBoolArg("-spendzeroconfchange", false),  coin_type);

    // vouts to the payees
    AppendOutputs(vecSend,txNew);
    if(!EnsureNoOutputsAreDust(txNew))
    {
        return {translate("Transaction output(s) amount too small"),false};
    }
    return SelectInputsProvideSignaturesAndFees(*this, coinSelector,vCoins,feeMode,txNew,changeOutputCreator,wtxNew);
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
            const auto walletDatabase = walletDatabaseEndpointFactory_.getDatabaseEndpoint();

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            addToWallet(wtxNew);

            // Notify that old coins are spent
            {
                std::set<uint256> updated_hashes;
                BOOST_FOREACH (const CTxIn& txin, wtxNew.vin)
                {
                    // notify only once
                    if (updated_hashes.find(txin.prevout.hash) != updated_hashes.end()) continue;

                    const CWalletTx* coinPtr = GetWalletTx(txin.prevout.hash);
                    if(!coinPtr)
                    {
                        LogPrintf("%s: Spending inputs not recorded in wallet - %s\n", __func__, txin.prevout.hash);
                        assert(coinPtr);
                    }
                    cachedUtxoBalanceCalculator_->recomputeCachedTxEntries(*coinPtr);
                    cachedTxDeltasCalculator_->recomputeCachedTxEntries(*coinPtr);
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
    std::unique_ptr<CReserveKey> reserveKey(new CReserveKey(*this));
    std::unique_ptr<ChangeOutputCreator> changeOutputCreator;
    if(requestedTransaction.changeAddress.empty()) changeOutputCreator.reset(new ChangeOutputCreator(*reserveKey));
    else changeOutputCreator.reset(new ChangeOutputCreator(requestedTransaction.changeAddress));

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
            requestedTransaction,
            *changeOutputCreator,
            *result.wtxNew);

    result.transactionCreationSucceeded = createTxResult.second;
    result.errorMessage = createTxResult.first;
    if(!result.transactionCreationSucceeded) return std::move(result);

    result.transactionCreationSucceeded = CommitTransaction(*result.wtxNew,*reserveKey);
    if(!result.transactionCreationSucceeded)
    {
        return std::move(result);
    }
    return std::move(result);
}

DBErrors CWallet::loadWallet()
{
    DBErrors nLoadWalletRet;
    {
        LOCK(cs_wallet);
        nLoadWalletRet = walletDatabaseEndpointFactory_.getDatabaseEndpoint()->LoadWallet(*static_cast<I_WalletLoader*>(this));
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
        int nMaxVersion = settings.GetArg("-upgradewallet", CLIENT_VERSION);
        if(nMaxVersion == CLIENT_VERSION) setMinVersion(FEATURE_LATEST);
        if(getVersion() > nMaxVersion)
        {
            LogPrintf("Unable to downgrade wallet version. -upgradewallet=<custom_version> might be incorrectly set\n");
            return DB_LOAD_FAIL;
        }
        setMaxVersion(nMaxVersion);

        if (!IsHDEnabled())
        {
            GenerateNewHDChain();
        }
        if(!settings.ParameterIsSet("-hdseed") && !settings.ParameterIsSet("-mnemonic"))
        {
            LogPrintf("%s -- Setting the best chain for wallet to the active chain...\n",__func__);
            SetBestChain(activeChain_.GetLocator());
        }
    }
    if(nLoadWalletRet == DB_LOAD_OK_RELOAD && !IsHDEnabled())
    {
        LogPrintf("%s -- Loaded wallet is not HD enabled. Non-HD wallets are invalid for this version\n",__func__);
        return DB_LOAD_FAIL;
    }
    return nLoadWalletRet;
}

void CWallet::loadWhitelistedVaults(const std::vector<SerializedScript>& vaultScripts)
{
    LOCK(cs_wallet);
    for(const SerializedScript& serializedVaultScript: vaultScripts)
    {
        CScript vaultScript(serializedVaultScript.begin(),serializedVaultScript.end());
        if(IsStakingVaultScript(vaultScript) && !HaveCScript(vaultScript))
        {
            if(AddCScript(vaultScript) && ownershipDetector_->isMine(CTxOut(0,vaultScript)) != isminetype::ISMINE_MANAGED_VAULT)
                RemoveCScript(vaultScript);
        }
    }
}

bool CWallet::SetAddressLabel(const CTxDestination& address, const std::string& strName)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet);
        fUpdated = addressBookManager_->setAddressLabel(address,strName);
    }
    NotifyAddressBookChanged(address, strName, computeMineType(*this, address, true) != isminetype::ISMINE_NO, (fUpdated ? "updated address label" : "new address label"));

    return walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteName(CBitcoinAddress(address).ToString(), strName);
}

const AddressBookManager& CWallet::getAddressBookManager() const
{
    return *addressBookManager_;
}

void CWallet::loadAddressLabel(const CTxDestination& address, const std::string newLabel)
{
    addressBookManager_->setAddressLabel(address,newLabel);
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        auto walletdb = walletDatabaseEndpointFactory_.getDatabaseEndpoint();
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
                defaultKeyPoolTopUp_? defaultKeyPoolTopUp_: settings.GetArg("-keypool", DEFAULT_KEYPOOL_SIZE),
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
        auto walletdb = walletDatabaseEndpointFactory_.getDatabaseEndpoint();
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

        auto walletdb = walletDatabaseEndpointFactory_.getDatabaseEndpoint();

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
    walletDatabaseEndpointFactory_.getDatabaseEndpoint()->ErasePool(nIndex);
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

void CWallet::LockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
    CWalletTx* txPtr = const_cast<CWalletTx*>(GetWalletTx(output.hash));
    if (txPtr != nullptr)
    {
        cachedUtxoBalanceCalculator_->recomputeCachedTxEntries(*txPtr);
        cachedTxDeltasCalculator_->recomputeCachedTxEntries(*txPtr);
    }
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

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}
CAmount CWallet::lockedCoinBalance(const UtxoOwnershipFilter& filter) const
{
    AssertLockHeld(cs_wallet);
    CAmount totalLockedBalance =0;
    for(const COutPoint& outPoint: setLockedCoins)
    {
        const CWalletTx* const walletTxPtr = GetWalletTx(outPoint.hash);
        if(walletTxPtr == nullptr) continue;
        if(filter.hasRequested(ownershipDetector_->isMine(walletTxPtr->vout[outPoint.n])) &&
            !outputTracker_->IsSpent(outPoint.hash,outPoint.n,0) &&
            confirmationNumberCalculator_.GetNumberOfBlockConfirmations(*walletTxPtr)>0)
        {
            totalLockedBalance += getCredit(*walletTxPtr,isminetype::ISMINE_SPENDABLE);
        }
    }
    return totalLockedBalance;
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

void CWallet::loadKeyPool(int nIndex, const CKeyPool &keypool)
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
        if(strMnemonicPassphrase.size() > 256)
            throw std::runtime_error(std::string(__func__) + ": Mnemonic passphrase is too long, must be at most 256 characters" );

        SecureVector vchMnemonic(strMnemonic.begin(), strMnemonic.end());
        SecureVector vchMnemonicPassphrase(strMnemonicPassphrase.begin(), strMnemonicPassphrase.end());

        if (!newHdChain.SetMnemonic(vchMnemonic, vchMnemonicPassphrase, true))
            throw std::runtime_error(std::string(__func__) + ": SetMnemonic failed");

        hdchainIsFromSeedRestore = !strMnemonic.empty();
    }

    if (!loadHDChain(newHdChain, false))
        throw std::runtime_error(std::string(__func__) + ": loadHDChain failed");

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
    if(!initializeDefaultKey())
    {
        throw std::runtime_error(std::string(__func__)+": Failed to initialize default key");
    }
    setMinVersion(FEATURE_HD); // ensure this wallet.dat can only be opened by clients supporting HD
}

bool CWallet::loadHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);

    if (!CCryptoKeyStore::SetHDChain(chain))
        return false;

    if (!memonly && !walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": WriteHDChain failed");

    return true;
}

bool CWallet::loadCryptedHDChain(const CHDChain& chain, bool memonly)
{
    AssertLockHeld(cs_wallet);

    if (!CCryptoKeyStore::SetCryptedHDChain(chain))
        return false;

    if (!memonly) {
        if (!walletDatabaseEndpointFactory_.getDatabaseEndpoint()->WriteCryptedHDChain(chain))
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
    AssertLockHeld(cs_wallet);
    walletStakingOnly = false;
    Lock();
}
void CWallet::UnlockForStakingOnly()
{
    AssertLockHeld(cs_wallet);
    if(IsFullyUnlocked())
    {
        walletStakingOnly = true;
    }
}