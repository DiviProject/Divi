#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <WalletTx.h>

VaultManager::VaultManager(
    ): managedScriptsLimits_()
    , transactionsByHash_()
{
}

VaultManager::~VaultManager()
{
}

void VaultManager::SyncTransaction(const CTransaction& tx, const CBlock *pblock)
{
    for(const CTxOut& output: tx.vout)
    {
        auto it = managedScriptsLimits_.find(output.scriptPubKey);
        if(it != managedScriptsLimits_.end())
        {
            CWalletTx walletTx(tx);
            transactionsByHash_.insert({tx.GetHash(),std::move(walletTx)});
            break;
        }
    }
}

void VaultManager::addManagedScript(const CScript& script, unsigned limit)
{
    managedScriptsLimits_.insert({script, limit});
}

UnspentOutputs VaultManager::getUTXOs() const
{
    UnspentOutputs outputs;
    auto managedScriptsLimitsCopy = managedScriptsLimits_;
    for(const auto& hashAndTransaction: transactionsByHash_)
    {
        uint256 hash = hashAndTransaction.first;
        const CWalletTx& tx = hashAndTransaction.second;
        for(unsigned outputIndex = 0; outputIndex < tx.vout.size(); ++outputIndex)
        {
            const CTxOut& output = tx.vout[outputIndex];
            auto it = managedScriptsLimitsCopy.find(output.scriptPubKey);
            if(it != managedScriptsLimitsCopy.end() && it->second > 0u)
            {
                outputs.insert(COutPoint{hash,outputIndex});
                --(it->second);
            }
        }
    }
    return outputs;
}