#include <SuperblockHelpers.h>
#include <chainparams.h>

bool OldIsValidLotteryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetLotteryBlockStartBlock() &&
            ((nBlockHeight % Params().GetLotteryBlockCycle()) == 0);
}

bool OldIsValidTreasuryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % Params().GetTreasuryPaymentsCycle()) == 0);
}

bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    const int minConflictHeight = Params().GetLotteryBlockCycle()*Params().GetTreasuryPaymentsCycle();
    if(nBlockHeight < minConflictHeight)
    {
        return OldIsValidLotteryBlockHeight(nBlockHeight);
    }
    else
    {
        int averageBlockCycleLength = (Params().GetLotteryBlockCycle()+Params().GetTreasuryPaymentsCycle())/2;
        return ((nBlockHeight - minConflictHeight) % averageBlockCycleLength) == 0;
    }
}

bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    const int minConflictHeight = Params().GetLotteryBlockCycle()*Params().GetTreasuryPaymentsCycle();
    if(nBlockHeight < minConflictHeight)
    {
        return OldIsValidTreasuryBlockHeight(nBlockHeight);
    }
    else
    {
        return IsValidLotteryBlockHeight(nBlockHeight-1);
    }
}

