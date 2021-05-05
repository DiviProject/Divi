#include <WalletTx.h>

#include <Logging.h>
#include <swifttx.h>
#include <script/standard.h>

void RelayTransaction(const CTransaction& tx);
void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll = false);

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

CWalletTx::CWalletTx()
{
    Init();
}

CWalletTx::CWalletTx(const CMerkleTx& txIn) : CMerkleTx(txIn)
{
    Init();
}

CWalletTx::CWalletTx(const CTransaction& txIn) : CMerkleTx(txIn)
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

void CWalletTx::RelayWalletTransaction()
{
    if (!IsCoinBase()) {
        if (GetNumberOfBlockConfirmations() == 0)
        {
            LogPrintf("Relaying wtx %s\n", ToStringShort());
            RelayTransaction((CTransaction) * this);
        }
    }
}
