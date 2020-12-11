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
using TransactionsByHash = std::map<uint256,CWalletTx>;

class WalletTransactionRecord;
class SpentOutputTracker;
class CChain;
class BlockMap;
class VaultManager
{
private:
    const CChain& activeChain_;
    const BlockMap& blockIndicesByHash_;
    mutable CCriticalSection cs_vaultManager_;
    int64_t transactionOrderingIndex_;
    std::unique_ptr<WalletTransactionRecord> walletTxRecord_;
    std::unique_ptr<SpentOutputTracker> outputTracker_;
    ManagedScripts managedScriptsLimits_;
public:
    VaultManager(
        const CChain& activeChain,
        const BlockMap& blockIndicesByHash);
    ~VaultManager();
    void SyncTransaction(const CTransaction& tx, const CBlock *pblock);
    void addManagedScript(const CScript& script, unsigned limit);
    UnspentOutputs getUTXOs() const;
};
#endif// VAULT_MANAGER_H