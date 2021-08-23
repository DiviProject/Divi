#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <SpentOutputTracker.h>
#include <I_VaultManagerDatabase.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

VaultManager::VaultManager(
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator
    ): confirmationsCalculator_(confirmationsCalculator)
    , cs_vaultManager_()
    , transactionOrderingIndex_(0)
    , walletTxRecord_(new WalletTransactionRecord(cs_vaultManager_))
    , outputTracker_(new SpentOutputTracker(*walletTxRecord_,confirmationsCalculator_))
    , managedScriptsLimits_()
{
}


VaultManager::VaultManager(
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
    I_VaultManagerDatabase& vaultManagerDB
    ): VaultManager(confirmationsCalculator)
{
    LOCK(cs_vaultManager_);
    vaultManagerDB.ReadManagedScripts(managedScriptsLimits_);
    bool continueLoadingTransactions = true;
    while(continueLoadingTransactions)
    {
        CWalletTx txToAdd;
        if(vaultManagerDB.ReadTx(transactionOrderingIndex_,txToAdd))
        {
            outputTracker_->UpdateSpends(txToAdd,transactionOrderingIndex_,true);
            ++transactionOrderingIndex_;
        }
        else
        {
            continueLoadingTransactions = false;
        }
    }
}

VaultManager::~VaultManager()
{
    outputTracker_.reset();
    walletTxRecord_.reset();
}

bool VaultManager::isManagedScript(const CScript& script) const
{
    AssertLockHeld(cs_vaultManager_);
    auto it = managedScriptsLimits_.find(script);
    if(it != managedScriptsLimits_.end())
    {
        return true;
    }
    return false;
}

bool VaultManager::transactionIsRelevant(const CTransaction& tx) const
{
    AssertLockHeld(cs_vaultManager_);
    for(const CTxIn& input: tx.vin)
    {
        const CWalletTx* walletTx = walletTxRecord_->GetWalletTx(input.prevout.hash);
        if(walletTx)
        {
            const CTxOut& output = walletTx->vout[input.prevout.n];
            if(isManagedScript(output.scriptPubKey))
            {
                return true;
            }
        }
    }
    for(const CTxOut& output: tx.vout)
    {
        if(output.nValue >0 && isManagedScript(output.scriptPubKey))
        {
            return true;
        }
    }
    return false;
}

void VaultManager::addTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit)
{
    LOCK(cs_vaultManager_);
    if(transactionIsRelevant(tx))
    {
        CWalletTx walletTx(tx);
        if(deposit) walletTx.mapValue["isVaultDeposit"] = "1";
        if(pblock) walletTx.SetMerkleBranch(*pblock);
        outputTracker_->UpdateSpends(walletTx,transactionOrderingIndex_,false);
        ++transactionOrderingIndex_;
    }
}

void VaultManager::addManagedScript(const CScript& script)
{
    LOCK(cs_vaultManager_);
    managedScriptsLimits_.insert({script});
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
        const int depth = confirmationsCalculator_.GetNumberOfBlockConfirmations(tx);
        if(depth < 1) continue;
        if((tx.IsCoinBase() || tx.IsCoinStake()) && confirmationsCalculator_.GetBlocksToMaturity(tx) > 0) continue;
        for(unsigned outputIndex = 0; outputIndex < tx.vout.size(); ++outputIndex)
        {
            const CTxOut& output = tx.vout[outputIndex];
            if(output.nValue >0 && isManagedScript(output.scriptPubKey) && !outputTracker_->IsSpent(hash,outputIndex))
            {
                outputs.emplace_back(&tx, outputIndex,depth,true);
            }
        }
    }
    return outputs;
}

const CWalletTx& VaultManager::getTransaction(const uint256& hash) const
{
    static CWalletTx dummyValue;

    LOCK(cs_vaultManager_);
    const CWalletTx* tx = walletTxRecord_->GetWalletTx(hash);

    if(!tx)
        return dummyValue;

    return *tx;
}

const ManagedScripts& VaultManager::getManagedScriptLimits() const
{
    return managedScriptsLimits_;
}
