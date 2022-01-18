#include <VaultManager.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <script/script.h>
#include <WalletTx.h>
#include <WalletTransactionRecord.h>
#include <SpentOutputTracker.h>
#include <I_VaultManagerDatabase.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <Logging.h>

constexpr const char* VAULT_DEPOSIT_DESCRIPTION = "isVaultDeposit";

VaultManager::VaultManager(
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
    I_VaultManagerDatabase& vaultManagerDB
    ): confirmationsCalculator_(confirmationsCalculator)
    , vaultManagerDB_(vaultManagerDB)
    , cs_vaultManager_()
    , walletTxRecord_(new WalletTransactionRecord(cs_vaultManager_))
    , outputTracker_(new SpentOutputTracker(*walletTxRecord_,confirmationsCalculator_))
    , managedScripts_()
    , whiteListedScripts_()
{
    LOCK(cs_vaultManager_);
    vaultManagerDB_.ReadManagedScripts(managedScripts_);
    bool continueLoadingTransactions = true;
    while(continueLoadingTransactions)
    {
        CWalletTx txToAdd;
        if(vaultManagerDB_.ReadTx(txToAdd))
        {
            outputTracker_->UpdateSpends(txToAdd,true);
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

bool VaultManager::transactionIsRelevant(const CTransaction& tx, bool checkOutputs, const CScript& outputScriptFilter) const
{
    AssertLockHeld(cs_vaultManager_);
    for(const CTxIn& input: tx.vin)
    {
        const CWalletTx* walletTx = walletTxRecord_->GetWalletTx(input.prevout.hash);
        if(walletTx)
        {
            const CTxOut& output = walletTx->vout[input.prevout.n];
            if(isManagedUTXO(*walletTx,output))
            {
                return true;
            }
        }
    }
    if(checkOutputs || !outputScriptFilter.empty())
    {
        for(const CTxOut& output: tx.vout)
        {
            if(output.nValue <= 0) continue;
            if(!outputScriptFilter.empty())
            {
                if(output.scriptPubKey == outputScriptFilter)
                {
                    return true;
                }
            }
            else
            {
                if(isManagedScript(output.scriptPubKey) )
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool VaultManager::transactionIsWhitelisted(const CTransaction& tx) const
{
    AssertLockHeld(cs_vaultManager_);
    if(whiteListedScripts_.size() == 0u) return false;
    for(const CTxOut& output: tx.vout)
    {
        if(output.nValue >0 && whiteListedScripts_.count(output.scriptPubKey) > 0u)
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
            if(!isManagedUTXO(*walletTx,output))
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

bool VaultManager::isManagedUTXO(const CWalletTx& walletTransaction,const CTxOut& output) const
{
    bool isAllowedByDepositDescription = true;
    if(walletTransaction.mapValue.count(VAULT_DEPOSIT_DESCRIPTION) > 0)
    {
        std::string depositDescription = walletTransaction.mapValue.find(VAULT_DEPOSIT_DESCRIPTION)->second;
        isAllowedByDepositDescription = depositDescription.empty() || depositDescription == output.scriptPubKey.ToString();
    }
    return output.nValue >0 && isAllowedByDepositDescription && isManagedScript(output.scriptPubKey);
}

void VaultManager::addTransaction(const CTransaction& tx, const CBlock *pblock, bool deposit,const CScript& scriptToFilterBy)
{
    LOCK(cs_vaultManager_);
    const bool blockIsNull = pblock==nullptr;
    const bool txIsWhiteListed = transactionIsWhitelisted(tx);
    const bool checkOutputs = txIsWhiteListed? false: (deposit || tx.IsCoinStake());

    if( txIsWhiteListed ||
        (!blockIsNull && transactionIsRelevant(tx, checkOutputs, scriptToFilterBy ) ) ||
        (blockIsNull && walletTxRecord_->GetWalletTx(tx.GetHash()) != nullptr) )
    {
        CWalletTx walletTx(tx);
        if(!blockIsNull) walletTx.SetMerkleBranch(*pblock);
        std::pair<CWalletTx*, bool> walletTxAndRecordStatus = outputTracker_->UpdateSpends(walletTx,false);

        if(deposit || txIsWhiteListed || (tx.IsCoinStake() && !allInputsAreKnown(tx)) )
        {
            walletTxAndRecordStatus.first->mapValue[VAULT_DEPOSIT_DESCRIPTION] = scriptToFilterBy.ToString();
        }
        if(!walletTxAndRecordStatus.second)
        {
            if(walletTxAndRecordStatus.first->UpdateTransaction(walletTx,blockIsNull) || deposit || txIsWhiteListed)
            {
                vaultManagerDB_.WriteTx(*walletTxAndRecordStatus.first);
            }
        }
        else
        {
            vaultManagerDB_.WriteTx(*walletTxAndRecordStatus.first);
        }
    }
}

void VaultManager::addManagedScript(const CScript& script)
{
    LOCK(cs_vaultManager_);
    if(managedScripts_.count(script) == 0)
    {
        managedScripts_.insert(script);
        vaultManagerDB_.WriteManagedScript(script);
    }
}
void VaultManager::addWhiteListedScript(const CScript& script)
{
    LOCK(cs_vaultManager_);
    if(whiteListedScripts_.count(script) == 0)
    {
        addManagedScript(script);
        whiteListedScripts_.insert(script);
    }
}
void VaultManager::removeManagedScript(const CScript& script)
{
    LOCK(cs_vaultManager_);
    if(managedScripts_.count(script) > 0)
    {
        managedScripts_.erase(script);
        vaultManagerDB_.EraseManagedScript(script);
    }
}

UnspentOutputs VaultManager::getManagedUTXOs(VaultUTXOFilters filter) const
{
    LOCK(cs_vaultManager_);
    UnspentOutputs outputs;
    auto managedScriptsLimitsCopy = managedScripts_;
    for(const auto& hashAndTransaction: walletTxRecord_->GetWalletTransactions())
    {
        uint256 hash = hashAndTransaction.first;
        const CWalletTx& tx = hashAndTransaction.second;
        if(!( (allInputsAreKnown(tx) && tx.IsCoinStake()) || tx.mapValue.count(VAULT_DEPOSIT_DESCRIPTION) > 0 )) continue;

        const int depth = confirmationsCalculator_.GetNumberOfBlockConfirmations(tx);
        if( depth < 0) continue;
        if( (filter & VaultUTXOFilters::CONFIRMED) > 0 && depth < 1) continue;
        if( (filter & VaultUTXOFilters::UNCONFIRMED) > 0 && depth != 0) continue;

        if(depth < 1 && (tx.IsCoinBase() || tx.IsCoinStake())) continue;
        const int blocksTillMaturity = confirmationsCalculator_.GetBlocksToMaturity(tx);
        if( (filter & VaultUTXOFilters::MATURED) > 0 && blocksTillMaturity > 0 ) continue;
        if( (filter & VaultUTXOFilters::INMATURE) > 0 && blocksTillMaturity < 1 ) continue;

        for(unsigned outputIndex = 0; outputIndex < tx.vout.size(); ++outputIndex)
        {
            const CTxOut& output = tx.vout[outputIndex];
            if(isManagedUTXO(tx,output) && !outputTracker_->IsSpent(hash,outputIndex,0))
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
    return managedScripts_;
}

bool VaultManager::Sync()
{
    // Force database sync
    return vaultManagerDB_.Sync(true);
}