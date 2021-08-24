#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <sync.h>
#include <Output.h>

class CScript;
using ManagedScripts = std::set<CScript>;
class COutPoint;
using UnspentOutputs = std::vector<COutput>;
class CTransaction;
class CBlock;
class CWalletTx;
class uint256;

class I_VaultManagerDatabase;
class WalletTransactionRecord;
class SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;

class VaultManager
{
private:
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator_;
    I_VaultManagerDatabase& vaultManagerDB_;
    mutable CCriticalSection cs_vaultManager_;
    uint64_t transactionOrderingIndex_;
    std::unique_ptr<WalletTransactionRecord> walletTxRecord_;
    std::unique_ptr<SpentOutputTracker> outputTracker_;
    ManagedScripts managedScripts_;

    bool isManagedScript(const CScript& script) const;
    bool transactionIsRelevant(const CTransaction& tx, bool checkOutputs) const;
    bool allInputsAreKnown(const CTransaction& tx) const;
    UnspentOutputs getUTXOs(bool onlyManagedCoins) const;
public:
    VaultManager(
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
        I_VaultManagerDatabase& vaultManagerDB);
    ~VaultManager();

    void addTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit);
    void addManagedScript(const CScript& script);
    void removeManagedScript(const CScript& script);
    UnspentOutputs getManagedUTXOs() const;
    UnspentOutputs getAllUTXOs() const;

    const CWalletTx& getTransaction(const uint256&) const;
    const ManagedScripts& getManagedScriptLimits() const;
};
#endif// VAULT_MANAGER_H
