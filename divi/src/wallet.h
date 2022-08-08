// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_H
#define BITCOIN_WALLET_H

#include <amount.h>
#include <base58address.h>
#include <pubkey.h>
#include <uint256.h>
#include <crypter.h>
#include <wallet_ismine.h>
#include <FeeRate.h>
#include <boost/foreach.hpp>
#include <utilstrencodings.h>
#include <tinyformat.h>
#include <NotificationInterface.h>
#include <merkletx.h>
#include <keypool.h>
#include <reservekey.h>
#include <OutputEntry.h>
#include <Output.h>
#include <I_StakingCoinSelector.h>
#include <I_WalletLoader.h>
#include <I_WalletDatabase.h>
#include <I_UtxoOwnershipDetector.h>
#include <AddressBookManager.h>
#include <LockedCoinsSet.h>
#include <AvailableCoinsType.h>
#include <CachedTransactionDeltas.h>

class I_CoinSelectionAlgorithm;
class CKeyMetadata;
class CKey;
class CBlock;
class CScript;
class CTransaction;
class CBlockIndex;
struct StakableCoin;
class I_AppendOnlyTransactionRecord;
class I_SpentOutputTracker;
class BlockMap;
class CChain;
class CCoinControl;
class COutput;
class CReserveKey;
class CScript;
class CWalletTx;
class CHDChain;
class CTxMemPool;
class COutPoint;
class CTxIn;
class I_MerkleTxConfirmationNumberCalculator;
class I_VaultManagerDatabase;
class VaultManager;
class CBlockLocator;
class I_BlockDataReader;
class I_WalletBalanceCalculator;
class AvailableUtxoCollector;
class I_WalletDatabaseEndpointFactory;

template <typename T>
class I_CachedTransactionDetailCalculator;
template <typename T>
class I_TransactionDetailCalculator;


bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);


/** (client) version numbers for particular wallet features */
enum WalletFeature {
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys
    FEATURE_HD = 120200,

    FEATURE_LATEST = 61000
};

/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
using CoinVector = std::vector<COutPoint>;
using Inputs = std::vector<CTxIn>;
using Outputs = std::vector<CTxOut>;

enum TransactionNotificationType
{
    NEW = 1 << 0,
    UPDATED = 1 << 1,
    SPEND_FROM = 1 << 3,
};
class I_WalletGuiNotifications
{
public:
    virtual ~I_WalletGuiNotifications(){}
    boost::signals2::signal<void(const CTxDestination& address, const std::string& label, bool isMine, std::string status)> NotifyAddressBookChanged;
    boost::signals2::signal<void(const uint256& hashTx, int status)> NotifyTransactionChanged;
    boost::signals2::signal<void(const std::string& title, int nProgress)> ShowProgress;
    boost::signals2::signal<void(bool fHaveWatchOnly)> NotifyWatchonlyChanged;
    boost::signals2::signal<void(bool fHaveMultiSig)> NotifyMultiSigChanged;
};

enum TransactionFeeMode
{
    SENDER_PAYS_FOR_TX_FEES,
    RECEIVER_PAYS_FOR_TX_FEES,
    SWEEP_FUNDS,
};
typedef std::map<std::string,std::string> TxTextMetadata;
struct TransactionCreationRequest
{
    const std::vector<std::pair<CScript, CAmount> >& scriptsToFund;
    TransactionFeeMode transactionFeeMode;
    AvailableCoinsType coin_type;
    const I_CoinSelectionAlgorithm* const coinSelectionAlgorithm;
    TxTextMetadata metadata;
    TransactionCreationRequest(
        const std::vector<std::pair<CScript, CAmount> >& scriptsToSendTo,
        TransactionFeeMode txFeeMode = TransactionFeeMode::SENDER_PAYS_FOR_TX_FEES,
        TxTextMetadata metadataToSet = TxTextMetadata(),
        AvailableCoinsType coinTypeSelected = AvailableCoinsType::ALL_SPENDABLE_COINS,
        const I_CoinSelectionAlgorithm* const algorithm = nullptr);
};
struct TransactionCreationResult
{
    bool transactionCreationSucceeded;
    std::string errorMessage;
    std::unique_ptr<CWalletTx> wtxNew;
    std::unique_ptr<CReserveKey> reserveKey;
    TransactionCreationResult();
    ~TransactionCreationResult();
    TransactionCreationResult(TransactionCreationResult&& other);
};

template <typename T>
isminetype computeMineType(const CKeyStore& keystore, const T& destinationOrScript, const bool parseVaultsAsSpendable);

class CWallet final:
    public CCryptoKeyStore,
    public NotificationInterface,
    public I_WalletGuiNotifications,
    public I_StakingWallet,
    protected I_WalletLoader
{
public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet
     *   except for:
     *      strWalletFile (immutable after instantiation)
     */
    mutable CCriticalSection cs_wallet;
    const std::string dbFilename() const;
private:

    const std::string strWalletFile;
    bool vaultModeEnabled_;
    LockedCoinsSet setLockedCoins;
    std::map<CKeyID, CHDPubKey> mapHdPubKeys; //<! memory map of HD extended pubkeys

    const I_WalletDatabaseEndpointFactory& walletDatabaseEndpointFactory_;
    const CChain& activeChain_;
    const BlockMap& blockIndexByHash_;
    const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator_;
    std::unique_ptr<AddressBookManager> addressBookManager_;
    std::unique_ptr<I_AppendOnlyTransactionRecord> transactionRecord_;
    std::unique_ptr<I_SpentOutputTracker> outputTracker_;
    std::unique_ptr<I_UtxoOwnershipDetector> ownershipDetector_;
    std::unique_ptr<AvailableUtxoCollector> availableUtxoCollector_;
    std::unique_ptr<I_TransactionDetailCalculator<CAmount>> utxoBalanceCalculator_;
    std::unique_ptr<I_CachedTransactionDetailCalculator<CAmount>> cachedUtxoBalanceCalculator_;
    std::unique_ptr<I_WalletBalanceCalculator> balanceCalculator_;
    std::unique_ptr<I_CachedTransactionDetailCalculator<CachedTransactionDeltas>> cachedTxDeltasCalculator_;
    mutable std::map<uint256,std::map<uint8_t, CachedTransactionDeltas>> cachedTransactionDeltasByHash_;

    int nWalletVersion;   //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletMaxVersion;//! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    CPubKey vchDefaultKey;
    int64_t nTimeFirstKey;

    int64_t timeOfLastChainTipUpdate;
    std::set<int64_t> setInternalKeyPool;
    std::set<int64_t> setExternalKeyPool;
    bool walletStakingOnly;
    int64_t defaultKeyPoolTopUp_;

    void deriveNewChildKey(const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal /*= false*/);
    void addTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType);
    bool initializeDefaultKey();

    // Notification interface methods
    void SetBestChain(const CBlockLocator& loc) override;
    void UpdatedBlockTip(const CBlockIndex *pindex) override;
    void SyncTransactions(const TransactionVector &tx, const CBlock *pblock, const TransactionSyncType) override;

    bool addToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, const TransactionSyncType syncType);
    bool addToWallet(const CWalletTx& wtxIn,bool blockDisconnection = false);

    void UpdateTimeFirstKey(int64_t nCreateTime);
    bool SatisfiesMinimumDepthRequirements(const CWalletTx* pcoin, int& nDepth, bool fOnlyConfirmed) const;
    int64_t SmartWalletTxTimestampEstimation(const CWalletTx& wtxIn);

    bool CanSupportFeature(enum WalletFeature wf);
    const CBlockIndex* GetNextUnsycnedBlockIndexInMainChain(bool syncFromGenesis = false);
    int64_t getTimestampOfFistKey() const;
    bool CanBePruned(const CWalletTx& wtx, const std::set<uint256>& unprunedTransactionIds, const int minimumNumberOfConfs) const;

    CAmount LockedCoinBalance(const UtxoOwnershipFilter& filter) const;

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, bool fExplicit = false);
    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

protected:

    // I_WalletLoader: load from disk methods
    void LoadWalletTransaction(const CWalletTx& wtxIn) override;
    bool LoadWatchOnly(const CScript& dest) override;
    bool LoadMinVersion(int nVersion) override;
    bool LoadMultiSig(const CScript& dest) override;
    bool LoadKey(const CKey& key, const CPubKey& pubkey) override;
    bool LoadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey) override;
    bool LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret) override;
    bool LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata, const bool updateFirstKeyTimestamp) override;
    bool LoadDefaultKey(const CPubKey& vchPubKey, bool updateDatabase) override;
    void LoadKeyPool(int nIndex, const CKeyPool &keypool) override;
    bool LoadCScript(const CScript& redeemScript) override;
    bool LoadCryptedHDChain(const CHDChain& chain, bool memonly) override;
    bool LoadHDPubKey(const CHDPubKey &hdPubKey) override;
    void ReserializeTransactions(const std::vector<uint256>& transactionIDs) override;
    void LoadAddressLabel(const CTxDestination& address, const std::string newLabel) override;
    bool LoadHDChain(const CHDChain& chain, bool memonly) override;

    CAmount GetBalanceByCoinType(AvailableCoinsType coinType) const;

public:
    explicit CWallet(
        const std::string& strWalletFileIn,
        const I_WalletDatabaseEndpointFactory& walletDatabaseEndpointFactory,
        const CChain& chain,
        const BlockMap& blockMap,
        const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator,
        const unsigned defaultKeyTopUp = 0u);
    ~CWallet();

    DBErrors loadWallet();
    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int getVersion();

    const AddressBookManager& getAddressBookManager() const;

    void verifySyncToActiveChain(const I_BlockDataReader& blockReader, bool startFromGenesis);
    void activateVaultMode();
    CKeyMetadata getKeyMetadata(const CBitcoinAddress& address) const;

    bool VerifyHDKeys() const;
    bool SetAddressLabel(const CTxDestination& address, const std::string& strName);

    bool HasAgedCoins() override;
    bool SelectStakeCoins(std::set<StakableCoin>& setCoins) const override;
    bool CanStakeCoins() const override;

    bool PruneWallet();

    bool IsUnlockedForStakingOnly() const;
    bool IsFullyUnlocked() const;
    void LockFully();
    void UnlockForStakingOnly();


    const CWalletTx* GetWalletTx(const uint256& hash) const;
    std::vector<const CWalletTx*> GetWalletTransactionReferences() const;
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    void AvailableCoins(
        std::vector<COutput>& vCoins,
        bool fOnlyConfirmed,
        AvailableCoinsType nCoinType = AvailableCoinsType::ALL_SPENDABLE_COINS) const;

    void LockCoin(const COutPoint& output);
    void UnlockCoin(const COutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(CoinVector& vOutpts);

    /**
     * HD Wallet Functions
     */
    bool AddHDPubKey(const CExtPubKey &extPubKey, bool fInternal);
    bool IsHDEnabled();
    void GenerateNewHDChain();
    bool GetDecryptedHDChain(CHDChain& hdChainRet);

    //  keystore implementation
    // Generate a new key
    CPubKey GenerateNewKey(uint32_t nAccountIndex, bool fInternal);
    bool AddVault(const CScript& vaultScript, const CBlock* pblock,const CTransaction& tx);
    bool RemoveVault(const CScript& vaultScript);

    bool Unlock(const SecureString& strWalletPassphrase, bool stakingOnly = false);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    //  keystore implementation
    bool HaveKey(const CKeyID &address) const override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    bool GetKey(const CKeyID &address, CKey& keyOut) const override;
    bool AddKeyPubKey(const CKey& key, const CPubKey& pubkey) override;

    bool AddCScript(const CScript& redeemScript) override;
    bool AddWatchOnly(const CScript& dest) override;
    bool RemoveWatchOnly(const CScript& dest) override;
    bool AddMultiSig(const CScript& dest) override;
    bool RemoveMultiSig(const CScript& dest) override;


    typedef std::multimap<int64_t, const CWalletTx*> TxItems;
    TxItems OrderedTxItems() const;

    bool IsChange(const CTxOut& txout) const;

    CAmount getChange(const CWalletTx& walletTransaction) const;
    CAmount getDebit(const CWalletTx& tx, const UtxoOwnershipFilter& filter) const;
    CAmount getCredit(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& filter) const;

    CAmount GetVaultedBalance() const;
    CAmount GetSpendableBalance() const;
    CAmount GetStakingBalance() const;

    CAmount GetBalance() const;
    CAmount GetUnconfirmedBalance() const;
    CAmount GetImmatureBalance() const;

    std::pair<std::string,bool> CreateTransaction(
        const std::vector<std::pair<CScript, CAmount> >& vecSend,
        const TransactionFeeMode feeMode,
        CWalletTx& wtxNew,
        CReserveKey& reservekey,
        const I_CoinSelectionAlgorithm* coinSelector,
        AvailableCoinsType coin_type = AvailableCoinsType::ALL_SPENDABLE_COINS);
    bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey);
    TransactionCreationResult SendMoney(const TransactionCreationRequest& requestedTransaction);

    bool NewKeyPool();
    bool TopUpKeyPool(unsigned int kpSize = 0);
    bool GetKeyFromPool(CPubKey& key, bool fInternal);
    unsigned int GetKeyPoolSize() const;

    // Implementation of I_KeypoolReserver
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fInternal) override;
    void KeepKey(int64_t nIndex) override;
    void ReturnKey(int64_t nIndex, bool fInternal) override;

};
#endif // BITCOIN_WALLET_H
