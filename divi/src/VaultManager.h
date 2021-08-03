#ifndef VAULT_MANAGER_H
#define VAULT_MANAGER_H
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <sync.h>

class CScript;
using ManagedScripts = std::map<CScript,unsigned>;
class COutPoint;
using UnspentOutputs = std::set<COutPoint>;
class CTransaction;
class CBlock;
class CWalletTx;
class uint256;

class I_VaultManagerDatabase;
class WalletTransactionRecord;
class SpentOutputTracker;
class I_MerkleTxConfirmationNumberCalculator;
class CChainParams;

class VaultManager
{
private:
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator_;
    const int requiredCoinbaseMaturity_;
    mutable CCriticalSection cs_vaultManager_;
    uint64_t transactionOrderingIndex_;
    std::unique_ptr<WalletTransactionRecord> walletTxRecord_;
    std::unique_ptr<SpentOutputTracker> outputTracker_;
    ManagedScripts managedScriptsLimits_;
public:
    VaultManager(
        const CChainParams& chainParameters,
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator);
    VaultManager(
        const CChainParams& chainParameters,
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
        I_VaultManagerDatabase& vaultManagerDB);
    ~VaultManager();
    void SyncTransaction(const CTransaction& tx, const CBlock *pblock);
    void addManagedScript(const CScript& script, unsigned limit);
    UnspentOutputs getUTXOs() const;
    const CWalletTx& GetTransaction(const uint256&) const;
    const ManagedScripts& GetManagedScriptLimits() const;
};
#endif// VAULT_MANAGER_H
