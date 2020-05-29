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

LotteryAndTreasuryBlockSubsidyIncentives::LotteryAndTreasuryBlockSubsidyIncentives(
    CChainParams& chainParameters
    ): chainParameters_(chainParameters)
{
}

bool LotteryAndTreasuryBlockSubsidyIncentives::OldIsValidLotteryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= chainParameters_.GetLotteryBlockStartBlock() &&
            ((nBlockHeight % chainParameters_.GetLotteryBlockCycle()) == 0);
}

bool LotteryAndTreasuryBlockSubsidyIncentives::OldIsValidTreasuryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= chainParameters_.GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % chainParameters_.GetTreasuryPaymentsCycle()) == 0);
}

bool LotteryAndTreasuryBlockSubsidyIncentives::IsValidLotteryBlockHeight(int nBlockHeight)
{
    const int minConflictHeight = chainParameters_.GetLotteryBlockCycle()*chainParameters_.GetTreasuryPaymentsCycle();
    if(nBlockHeight < minConflictHeight)
    {
        return OldIsValidLotteryBlockHeight(nBlockHeight);
    }
    else
    {
        int averageBlockCycleLength = (chainParameters_.GetLotteryBlockCycle()+chainParameters_.GetTreasuryPaymentsCycle())/2;
        return ((nBlockHeight - minConflictHeight) % averageBlockCycleLength) == 0;
    }
}
bool LotteryAndTreasuryBlockSubsidyIncentives::IsValidTreasuryBlockHeight(int nBlockHeight)
{
    const int minConflictHeight = chainParameters_.GetLotteryBlockCycle()*chainParameters_.GetTreasuryPaymentsCycle();
    if(nBlockHeight < minConflictHeight)
    {
        return OldIsValidTreasuryBlockHeight(nBlockHeight);
    }
    else
    {
        return IsValidLotteryBlockHeight(nBlockHeight-1);
    }
}
