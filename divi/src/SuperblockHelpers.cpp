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
    return OldIsValidLotteryBlockHeight(nBlockHeight);
}

bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    return OldIsValidTreasuryBlockHeight(nBlockHeight);
}

