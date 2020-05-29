#include <test/test_only.h>
#include <SuperblockHelpers.h>
#include <chainparams.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(SuperblockHelper_tests)

int getMaxBlockConflictHeight(CChainParams& chainParams)
{
    int blockLotteryStart = chainParams.GetLotteryBlockStartBlock();
    int lotteryCycleLength = chainParams.GetLotteryBlockCycle();
    int blockTreasuryStart = chainParams.GetTreasuryPaymentsStartBlock();
    int treasuryCycleLength = chainParams.GetTreasuryPaymentsCycle();
    
    return 10*(lotteryCycleLength)*(treasuryCycleLength) + blockLotteryStart + blockTreasuryStart;
}

void CheckForBlockTypeConflicts(CChainParams& chainParams,LotteryAndTreasuryBlockSubsidyIncentives& incentives)
{
    for(int blockHeight = 0; blockHeight <= getMaxBlockConflictHeight(chainParams); blockHeight++)
    {
        if(incentives.IsValidLotteryBlockHeight(blockHeight) && 
            incentives.IsValidTreasuryBlockHeight(blockHeight))
        {
            BOOST_CHECK_MESSAGE(false, "A treasury block cannot also be a lottery block! Failure at height " << blockHeight << "!" );
            break;
        }
    }
}
BOOST_AUTO_TEST_CASE(willNeverAssertAHeightIsValidForBothTreasuryAndLottery)
{
    {
        CChainParams& chainParams = Params(CBaseChainParams::MAIN);
        LotteryAndTreasuryBlockSubsidyIncentives incentives(chainParams);
        CheckForBlockTypeConflicts(chainParams, incentives);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        LotteryAndTreasuryBlockSubsidyIncentives incentives(chainParams);
        CheckForBlockTypeConflicts(chainParams, incentives);
    }
}

void checkBackwardCompatibilityOfSuperblockValidity(LotteryAndTreasuryBlockSubsidyIncentives& incentives)
{
    for(int blockHeight = 0; blockHeight < incentives.getTransitionHeight(); blockHeight++)
    {
        if( Legacy::IsValidLotteryBlockHeight(blockHeight, incentives.getChainParameters()) != incentives.IsValidLotteryBlockHeight(blockHeight) ||
            Legacy::IsValidTreasuryBlockHeight(blockHeight, incentives.getChainParameters()) != incentives.IsValidTreasuryBlockHeight(blockHeight))
        {
            BOOST_CHECK_MESSAGE(false, "Backward compatibility prior to transition height at block: " << blockHeight << "!");
            return;
        }
    }
}

BOOST_AUTO_TEST_CASE(willBeBackWardCompatiblePriorToTransitionHeight)
{
    {
        CChainParams& chainParams = Params(CBaseChainParams::MAIN);
        LotteryAndTreasuryBlockSubsidyIncentives incentives(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(incentives);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        LotteryAndTreasuryBlockSubsidyIncentives incentives(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(incentives);
    }
}

BOOST_AUTO_TEST_SUITE_END()
