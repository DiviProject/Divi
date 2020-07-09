#include <SuperblockHeightValidator.h>

#include <chainparams.h>
#include <LegacyBlockSubsidies.h>

SuperblockHeightValidator::SuperblockHeightValidator(
    const CChainParams& chainParameters
    ): chainParameters_(chainParameters)
    , transitionHeight_(chainParameters_.GetLotteryBlockCycle()*chainParameters_.GetTreasuryPaymentsCycle())
    , superblockCycleLength_((chainParameters_.GetLotteryBlockCycle()+chainParameters_.GetTreasuryPaymentsCycle())/2)
{
}

bool SuperblockHeightValidator::IsValidLotteryBlockHeight(int nBlockHeight) const
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
bool SuperblockHeightValidator::IsValidTreasuryBlockHeight(int nBlockHeight) const
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

int SuperblockHeightValidator::getTransitionHeight() const
{
    return transitionHeight_;
}

const CChainParams& SuperblockHeightValidator::getChainParameters() const
{
    return chainParameters_;
}

int SuperblockHeightValidator::GetTreasuryBlockPaymentCycle(int nBlockHeight) const
{
    return (nBlockHeight < transitionHeight_)? chainParameters_.GetTreasuryPaymentsCycle():
        ((nBlockHeight <= transitionHeight_+1)? chainParameters_.GetTreasuryPaymentsCycle()+1: superblockCycleLength_);
}
int SuperblockHeightValidator::GetLotteryBlockPaymentCycle(int nBlockHeight) const
{
    return (nBlockHeight < transitionHeight_)? chainParameters_.GetLotteryBlockCycle(): superblockCycleLength_;
}