#include <VaultManager.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <SpentOutputTracker.h>
#include <I_VaultManagerDatabase.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

constexpr const char* VAULT_DEPOSIT_DESCRIPTION = "isVaultDeposit";

VaultManager::VaultManager(
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
    I_VaultManagerDatabase& vaultManagerDB
    ): confirmationsCalculator_(confirmationsCalculator)
    , cs_vaultManager_()
    , transactionOrderingIndex_(0)
    , walletTxRecord_(new WalletTransactionRecord(cs_vaultManager_))
    , outputTracker_(new SpentOutputTracker(*walletTxRecord_,confirmationsCalculator_))
    , managedScripts_()
{
    LOCK(cs_vaultManager_);
    vaultManagerDB.ReadManagedScripts(managedScripts_);
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
    auto it = managedScripts_.find(script);
    if(it != managedScripts_.end())
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

bool VaultManager::allInputsAreKnown(const CTransaction& tx) const
{
    AssertLockHeld(cs_vaultManager_);
    for(const CTxIn& input: tx.vin)
    {
        const CWalletTx* walletTx = walletTxRecord_->GetWalletTx(input.prevout.hash);
        if(walletTx)
        {
            const CTxOut& output = walletTx->vout[input.prevout.n];
            if(!isManagedScript(output.scriptPubKey))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

void VaultManager::addTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit)
{
    LOCK(cs_vaultManager_);
    if(transactionIsRelevant(tx))
    {
        CWalletTx walletTx(tx);
        if(deposit) walletTx.mapValue[VAULT_DEPOSIT_DESCRIPTION] = "1";
        if(pblock) walletTx.SetMerkleBranch(*pblock);
        std::pair<CWalletTx*, bool> walletTxAndRecordStatus = outputTracker_->UpdateSpends(walletTx,transactionOrderingIndex_,false);
        if(!walletTxAndRecordStatus.second)
        {
            walletTxAndRecordStatus.first->UpdateTransaction(walletTx,pblock==nullptr);
        }
        else
        {
            ++transactionOrderingIndex_;
        }
    }
}

void VaultManager::addManagedScript(const CScript& script)
{
    LOCK(cs_vaultManager_);
    managedScripts_.insert(script);
}

UnspentOutputs VaultManager::getUTXOs(bool onlyManagedCoins) const
{
    LOCK(cs_vaultManager_);
    UnspentOutputs outputs;
    auto managedScriptsLimitsCopy = managedScripts_;
    for(const auto& hashAndTransaction: walletTxRecord_->mapWallet)
    {
        uint256 hash = hashAndTransaction.first;
        const CWalletTx& tx = hashAndTransaction.second;
        if(onlyManagedCoins)
        {
            if(!( allInputsAreKnown(tx) && (tx.IsCoinStake() || tx.mapValue.count(VAULT_DEPOSIT_DESCRIPTION) > 0) )) continue;
        }
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

UnspentOutputs VaultManager::getAllUTXOs() const
{
    return getUTXOs(false);
}
UnspentOutputs VaultManager::getManagedUTXOs() const
{
    return getUTXOs(true);
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
    return managedScripts_;
}
