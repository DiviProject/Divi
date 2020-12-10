#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <SpentOutputTracker.h>
#include <primitives/block.h>

VaultManager::VaultManager(
    const CChain& activeChain,
    const BlockMap& blockIndicesByHash,
    const std::string& dbFilename
    ): activeChain_(activeChain)
    , blockIndicesByHash_(blockIndicesByHash)
    , cs_vaultManager_()
    , transactionOrderingIndex_(0)
    , walletTxRecord_(new WalletTransactionRecord(cs_vaultManager_,dbFilename))
    , outputTracker_(new SpentOutputTracker(*walletTxRecord_))
    , managedScriptsLimits_()
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
            CMerkleTx merkleTx(tx,activeChain_,blockIndicesByHash_);
            if(pblock) merkleTx.SetMerkleBranch(*pblock);
            outputTracker_->UpdateSpends(merkleTx,transactionOrderingIndex_,true);
            ++transactionOrderingIndex_;
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
    for(const auto& hashAndTransaction: walletTxRecord_->mapWallet)
    {
        uint256 hash = hashAndTransaction.first;
        const CWalletTx& tx = hashAndTransaction.second;

        for(unsigned outputIndex = 0; outputIndex < tx.vout.size(); ++outputIndex)
        {
            const CTxOut& output = tx.vout[outputIndex];
            auto it = managedScriptsLimitsCopy.find(output.scriptPubKey);
            if(it != managedScriptsLimitsCopy.end() && it->second > 0u && !outputTracker_->IsSpent(hash,outputIndex))
            {
                outputs.insert(COutPoint{hash,outputIndex});
                --(it->second);
            }
        }
    }
    return outputs;
}