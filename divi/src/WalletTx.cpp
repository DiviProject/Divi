#include <WalletTx.h>

#include <Logging.h>

void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n")) {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

CWalletTx::CWalletTx(): CMerkleTx()
{
    Init();
}

CWalletTx::CWalletTx(
    const CTransaction& txIn
    ): CMerkleTx(txIn)
{
    Init();
}

void CWalletTx::Init()
{
    mapValue.clear();
    vOrderForm.clear();
    fTimeReceivedIsTxTime = false;
    nTimeReceived = 0;
    nTimeSmart = 0;
    createdByMe = false;
    strFromAccount.clear();
    fDebitCached = false;
    fCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fChangeCached = false;
    nDebitCached = 0;
    nCreditCached = 0;
    nWatchDebitCached = 0;
    nWatchCreditCached = 0;
    nChangeCached = 0;
    nOrderPos = -1;
}

//! make sure balances are recalculated
void CWalletTx::RecomputeCachedQuantities()
{
    fCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fDebitCached = false;
    fChangeCached = false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

bool CWalletTx::UpdateTransaction(const CWalletTx& other, bool reorg)
{
    // Merge
    bool walletTransactionHasBeenUpdated = false;
    if (other.hashBlock != 0 && other.hashBlock != hashBlock)
    {
        hashBlock = other.hashBlock;
        walletTransactionHasBeenUpdated = true;
    }
    if (other.merkleBranchIndex != -1 && (other.vMerkleBranch != vMerkleBranch || other.merkleBranchIndex != merkleBranchIndex))
    {
        vMerkleBranch = other.vMerkleBranch;
        merkleBranchIndex = other.merkleBranchIndex;
        walletTransactionHasBeenUpdated = true;
    }
    if (other.createdByMe && other.createdByMe != createdByMe)
    {
        createdByMe = other.createdByMe;
        walletTransactionHasBeenUpdated = true;
    }
    if(reorg && !other.createdByMe && !other.MerkleBranchIsSet() && MerkleBranchIsSet())
    {
        ClearMerkleBranch();
        walletTransactionHasBeenUpdated = true;
    }
    return walletTransactionHasBeenUpdated;
}