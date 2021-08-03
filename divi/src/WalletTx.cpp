#include <WalletTx.h>

#include <Logging.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

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

CWalletTx::CWalletTx(
    const CTransaction& txIn,
    const int requiredCoinbaseMaturity,
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator
    ): CMerkleTx(txIn)
    , confirmationsCalculator_(confirmationsCalculator)
    , requiredCoinbaseMaturity_(requiredCoinbaseMaturity)
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
    fImmatureCreditCached = false;
    fAvailableCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fChangeCached = false;
    nDebitCached = 0;
    nCreditCached = 0;
    nImmatureCreditCached = 0;
    nAvailableCreditCached = 0;
    nWatchDebitCached = 0;
    nWatchCreditCached = 0;
    nAvailableWatchCreditCached = 0;
    nImmatureWatchCreditCached = 0;
    nChangeCached = 0;
    nOrderPos = -1;
}

//! make sure balances are recalculated
void CWalletTx::RecomputeCachedQuantities()
{
    fCreditCached = false;
    fAvailableCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fDebitCached = false;
    fChangeCached = false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int64_t CWalletTx::GetComputedTxTime() const
{
    int64_t nTime = GetTxTime();
    return nTime;
}
int CWalletTx::GetNumberOfBlockConfirmations() const
{
    return confirmationsCalculator_.GetNumberOfBlockConfirmations(*this);
}
int CWalletTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (requiredCoinbaseMaturity_ + 1) - confirmationsCalculator_.GetNumberOfBlockConfirmations(*this));
}