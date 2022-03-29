#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <sync.h>
#include <Output.h>
#include <NotificationInterface.h>
#include <script/script.h>

using ManagedScripts = std::set<CScript>;
class COutPoint;
using UnspentOutputs = std::vector<COutput>;
class CTransaction;
class CBlock;
class CWalletTx;
class uint256;
class CTxOut;

class I_VaultManagerDatabase;
class WalletTransactionRecord;
class I_SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;

enum VaultUTXOFilters
{
    NO_FILTER = 0,
    CONFIRMED = 1 << 0,
    UNCONFIRMED = 1 << 1,
    MATURED = 1 << 2,
    INMATURE = 1 << 3,
    CONFIRMED_AND_MATURED = CONFIRMED | MATURED
};
class VaultManager final: public NotificationInterface
{
private:
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator_;
    I_VaultManagerDatabase& vaultManagerDB_;
    mutable CCriticalSection cs_vaultManager_;
    std::unique_ptr<WalletTransactionRecord> walletTxRecord_;
    std::unique_ptr<I_SpentOutputTracker> outputTracker_;
    ManagedScripts managedScripts_;
    ManagedScripts whiteListedScripts_;

    bool isManagedScript(const CScript& script) const;
    bool transactionIsWhitelisted(const CTransaction& tx) const;
    bool transactionIsRelevant(const CTransaction& tx, bool checkOutputs,const CScript& outputScriptFilter) const;
    bool allInputsAreKnown(const CTransaction& tx) const;
    bool isManagedUTXO(const CWalletTx& walletTransaction,const CTxOut& output) const;

    // Notification interface methods
    void SyncTransactions(const TransactionVector &tx, const CBlock *pblock, const TransactionSyncType) override {};
    void SetBestChain(const CBlockLocator& loc) override {};
    void UpdatedBlockTip(const CBlockIndex *pindex) override {};

    void addSingleTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit,const CScript& scriptToFilterBy);
public:
    VaultManager(
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
        I_VaultManagerDatabase& vaultManagerDB);
    ~VaultManager();

    void addTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit,const CScript& scriptToFilterBy = CScript());
    void addManagedScript(const CScript& script);
    void addWhiteListedScript(const CScript& script);
    void removeManagedScript(const CScript& script);
    UnspentOutputs getManagedUTXOs(VaultUTXOFilters filter = VaultUTXOFilters::CONFIRMED_AND_MATURED) const;

    const CWalletTx& getTransaction(const uint256&) const;
    const ManagedScripts& getManagedScriptLimits() const;
    bool Sync();
};
#endif// VAULT_MANAGER_H
