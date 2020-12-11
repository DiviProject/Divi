#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <SpentOutputTracker.h>

VaultManager::VaultManager(
    const CChain& activeChain,
    const BlockMap& blockIndicesByHash
    ): activeChain_(activeChain)
    , blockIndicesByHash_(blockIndicesByHash)
    , cs_vaultManager_()
    , transactionOrderingIndex_(0)
    , walletTxRecord_(new WalletTransactionRecord(cs_vaultManager_))
    , outputTracker_(new SpentOutputTracker(*walletTxRecord_))
    , managedScriptsLimits_()
{
}

VaultManager::~VaultManager()
{
}

void VaultManager::SyncTransaction(const CTransaction& tx, const CBlock *pblock)
{
    LOCK(cs_vaultManager_);
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
    LOCK(cs_vaultManager_);
    managedScriptsLimits_.insert({script, limit});
}

UnspentOutputs VaultManager::getUTXOs() const
{
    LOCK(cs_vaultManager_);
    UnspentOutputs outputs;
    auto managedScriptsLimitsCopy = managedScriptsLimits_;
    for(const auto& hashAndTransaction: walletTxRecord_->mapWallet)
    {
        uint256 hash = hashAndTransaction.first;
        const CWalletTx& tx = hashAndTransaction.second;
        if(tx.GetNumberOfBlockConfirmations()<1) continue;
        if((tx.IsCoinBase() || tx.IsCoinStake()) && tx.GetBlocksToMaturity() > 0) continue;
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