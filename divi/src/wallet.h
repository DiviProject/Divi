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
class WalletBalanceCalculator;

bool IsFinalTx(const CTransaction& tx, const CChain& activeChain, int nBlockHeight = 0 , int64_t nBlockTime = 0);


/** (client) version numbers for particular wallet features */
enum WalletFeature {
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys
    FEATURE_HD = 120200,

    FEATURE_LATEST = 61000
};

enum class AvailableCoinsType {
    ALL_SPENDABLE_COINS = 0,                    // find masternode outputs including locked ones (use with caution)
    STAKABLE_COINS = 1,                          // UTXO's that are valid for staking
    OWNED_VAULT_COINS = 2
};

/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
enum TransactionCreditFilters
{
    REQUIRE_NOTHING = 0,
    REQUIRE_UNSPENT = 1,
    REQUIRE_LOCKED = 1 << 1,
    REQUIRE_UNLOCKED  = 1 << 2,
    REQUIRE_AVAILABLE_TYPE  = 1 << 3,
};
using LockedCoinsSet = std::set<COutPoint>;
using CoinVector = std::vector<COutPoint>;
using AddressBook = std::map<CTxDestination, AddressLabel>;
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

class AddressBookManager
{
public:
    typedef std::map<std::string,CTxDestination> LastDestinationByLabel;
private:
    AddressBook mapAddressBook;
    LastDestinationByLabel destinationByLabel_;
public:
    AddressBookManager();
    const AddressBook& GetAddressBook() const;
    const LastDestinationByLabel& GetLastDestinationByLabel() const;
    bool SetAddressLabel(
        const CTxDestination& address,
        const std::string strName);
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

class CWallet final:
    public CCryptoKeyStore,
    public NotificationInterface,
    public I_UtxoOwnershipDetector,
    public virtual I_KeypoolReserver,
    public I_WalletGuiNotifications,
    public I_StakingWallet,
    protected I_WalletLoader
{
public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet
     *   except for:
     *      fFileBacked (immutable after instantiation)
     *      strWalletFile (immutable after instantiation)
     */
    mutable CCriticalSection cs_wallet;
    bool isBackedByFile() const;
    const std::string dbFilename() const;
private:
    void SetNull();

    bool fFileBacked;
    std::string strWalletFile;
    const CChain& activeChain_;
    const BlockMap& blockIndexByHash_;
    const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator_;
    std::unique_ptr<AddressBookManager> addressBookManager_;
    std::shared_ptr<VaultManager> vaultManager_;
    std::unique_ptr<I_AppendOnlyTransactionRecord> transactionRecord_;
    std::unique_ptr<I_SpentOutputTracker> outputTracker_;
    std::unique_ptr<WalletBalanceCalculator> balanceCalculator_;

    int nWalletVersion;   //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletMaxVersion;//! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    LockedCoinsSet setLockedCoins;
    std::map<CKeyID, CHDPubKey> mapHdPubKeys; //<! memory map of HD extended pubkeys
    CPubKey vchDefaultKey;
    int64_t nTimeFirstKey;

    int64_t timeOfLastChainTipUpdate;
    std::set<int64_t> setInternalKeyPool;
    std::set<int64_t> setExternalKeyPool;
    bool walletStakingOnly;
    bool allowSpendingZeroConfirmationOutputs;
    int64_t defaultKeyPoolTopUp;

    void DeriveNewChildKey(const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal /*= false*/);

    void AddTransactions(const TransactionVector& txs, const CBlock* pblock,const TransactionSyncType syncType);

    // Notification interface methods
    void SetBestChain(const CBlockLocator& loc) override;
    void UpdatedBlockTip(const CBlockIndex *pindex) override;
    void SyncTransactions(const TransactionVector &tx, const CBlock *pblock, const TransactionSyncType) override;

    isminetype isMine(const CScript& scriptPubKey) const;
    isminetype isMine(const CTxIn& txin) const;
    bool isMine(const CTransaction& tx) const;

    void UpdateTimeFirstKey(int64_t nCreateTime);
    bool SatisfiesMinimumDepthRequirements(const CWalletTx* pcoin, int& nDepth, bool fOnlyConfirmed) const;
    bool IsTrusted(const CWalletTx& walletTransaction) const;
    int64_t SmartWalletTxTimestampEstimation(const CWalletTx& wtxIn);

    bool CanSupportFeature(enum WalletFeature wf);
    const CBlockIndex* GetNextUnsycnedBlockIndexInMainChain(bool syncFromGenesis = false);
    int64_t getTimestampOfFistKey() const;
    bool CanBePruned(const CWalletTx& wtx, const std::set<uint256>& unprunedTransactionIds, const int minimumNumberOfConfs) const;

    CAmount GetDebit(const CTxIn& txin, const UtxoOwnershipFilter& filter) const;
    CAmount ComputeCredit(const CTxOut& txout, const UtxoOwnershipFilter& filter) const;
    CAmount ComputeChange(const CTxOut& txout) const;
    CAmount ComputeDebit(const CTransaction& tx, const UtxoOwnershipFilter& filter) const;
    CAmount ComputeCredit(const CWalletTx& tx, const UtxoOwnershipFilter& filter, int creditFilterFlags = REQUIRE_NOTHING) const;
    bool DebitsFunds(const CTransaction& tx) const;
    bool DebitsFunds(const CWalletTx& tx,const UtxoOwnershipFilter& filter) const;

protected:

    // CWalletDB: load from disk methods
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

    void InitializeDatabaseBackend();
    CAmount GetBalanceByCoinType(AvailableCoinsType coinType) const;

public:
    explicit CWallet(
        const CChain& chain,
        const BlockMap& blockMap,
        const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator);
    explicit CWallet(
        const std::string& strWalletFileIn,
        const CChain& chain,
        const BlockMap& blockMap,
        const I_MerkleTxConfirmationNumberCalculator& confirmationNumberCalculator);
    ~CWallet();

    DBErrors LoadWallet();
    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, bool fExplicit = false);
    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);
    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion();

    const AddressBookManager& GetAddressBookManager() const;
    const I_MerkleTxConfirmationNumberCalculator& getConfirmationCalculator() const;
    std::unique_ptr<I_WalletDatabase> GetDatabaseBackend() const;

    std::string getWalletIdentifier() const;
    bool InitializeDefaultKey();
    void SetDefaultKeyTopUp(int64_t keypoolTopUp);

    void verifySyncToActiveChain(const I_BlockDataReader& blockReader, bool startFromGenesis);
    void toggleSpendingZeroConfirmationOutputs();
    void activateVaultMode(
        std::shared_ptr<VaultManager> vaultManager);
    CKeyMetadata getKeyMetadata(const CBitcoinAddress& address) const;

    bool VerifyHDKeys() const;
    bool SetAddressLabel(const CTxDestination& address, const std::string& strName);

    bool HasAgedCoins() override;
    bool SelectStakeCoins(std::set<StakableCoin>& setCoins) const override;
    bool CanStakeCoins() const override;

    isminetype isMine(const CTxOut& txout) const override;
    bool IsSpent(const CWalletTx& wtx, unsigned int n) const;
    bool PruneWallet();

    bool IsUnlockedForStakingOnly() const;
    bool IsFullyUnlocked() const;
    void LockFully();


    const CWalletTx* GetWalletTx(const uint256& hash) const;
    std::vector<const CWalletTx*> GetWalletTransactionReferences() const;
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool IsAvailableForSpending(
        const CWalletTx* pcoin,
        unsigned int i,
        bool& fIsSpendable,
        AvailableCoinsType coinType = AvailableCoinsType::ALL_SPENDABLE_COINS) const;
    void AvailableCoins(
        std::vector<COutput>& vCoins,
        bool fOnlyConfirmed = true,
        AvailableCoinsType nCoinType = AvailableCoinsType::ALL_SPENDABLE_COINS,
        CAmount nExactValue = CAmount(0)) const;

    bool IsLockedCoin(const uint256& hash, unsigned int n) const;
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

    bool AddToWallet(const CWalletTx& wtxIn,bool blockDisconnection = false);
    bool AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, const TransactionSyncType syncType);
    bool AllInputsAreMine(const CWalletTx& walletTransaction) const;
    isminetype isMine(const CTxDestination& dest) const;

    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CWalletTx& walletTransaction) const;

    CAmount GetDebit(const CWalletTx& tx, const UtxoOwnershipFilter& filter) const;
    CAmount GetCredit(const CWalletTx& walletTransaction, const UtxoOwnershipFilter& filter) const;
    CAmount ComputeChange(const CTransaction& tx) const;

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
