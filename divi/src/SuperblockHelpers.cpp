#include <SuperblockHelpers.h>
#include <chainparams.h>

bool Legacy::IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetLotteryBlockStartBlock() &&
            ((nBlockHeight % chainParams.GetLotteryBlockCycle()) == 0);
}

bool Legacy::IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % chainParams.GetTreasuryPaymentsCycle()) == 0);
}

bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    const int minConflictHeight = Params().GetLotteryBlockCycle()*Params().GetTreasuryPaymentsCycle();
    if(nBlockHeight < minConflictHeight)
    {
        return Legacy::IsValidLotteryBlockHeight(nBlockHeight,Params());
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
        return Legacy::IsValidTreasuryBlockHeight(nBlockHeight,Params());
    }
    else
    {
        return IsValidLotteryBlockHeight(nBlockHeight-1);
    }
}

LotteryAndTreasuryBlockSubsidyIncentives::LotteryAndTreasuryBlockSubsidyIncentives(
    CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , transitionHeight_(chainParameters_.GetLotteryBlockCycle()*chainParameters_.GetTreasuryPaymentsCycle())
    , superblockCycleLength_((chainParameters_.GetLotteryBlockCycle()+chainParameters_.GetTreasuryPaymentsCycle())/2)
{
}

bool LotteryAndTreasuryBlockSubsidyIncentives::IsValidLotteryBlockHeight(int nBlockHeight)
{
    if(nBlockHeight < transitionHeight_)
    {
        return Legacy::IsValidLotteryBlockHeight(nBlockHeight,chainParameters_);
    }
    else
    {
        return ((nBlockHeight - transitionHeight_) % superblockCycleLength_) == 0;
    }
}
bool LotteryAndTreasuryBlockSubsidyIncentives::IsValidTreasuryBlockHeight(int nBlockHeight)
{
    if(nBlockHeight < transitionHeight_)
    {
        return Legacy::IsValidTreasuryBlockHeight(nBlockHeight,chainParameters_);
    }
    else
    {
        return IsValidLotteryBlockHeight(nBlockHeight-1);
    }
}

int LotteryAndTreasuryBlockSubsidyIncentives::getTransitionHeight() const
{
    return transitionHeight_;
}

CChainParams& LotteryAndTreasuryBlockSubsidyIncentives::getChainParameters() const
{
    return chainParameters_;
}