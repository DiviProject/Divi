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

void CheckForBlockTypeConflicts(CChainParams& chainParams,SuperblockSubsidyHeightValidator& validator)
{
    for(int blockHeight = 0; blockHeight <= getMaxBlockConflictHeight(chainParams); blockHeight++)
    {
        if(validator.IsValidLotteryBlockHeight(blockHeight) && 
            validator.IsValidTreasuryBlockHeight(blockHeight))
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
        SuperblockSubsidyHeightValidator validator(chainParams);
        CheckForBlockTypeConflicts(chainParams, validator);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        SuperblockSubsidyHeightValidator validator(chainParams);
        CheckForBlockTypeConflicts(chainParams, validator);
    }
}

void checkBackwardCompatibilityOfSuperblockValidity(SuperblockSubsidyHeightValidator& validator)
{
    for(int blockHeight = 0; blockHeight < validator.getTransitionHeight(); blockHeight++)
    {
        if( Legacy::IsValidLotteryBlockHeight(blockHeight, validator.getChainParameters()) != validator.IsValidLotteryBlockHeight(blockHeight) ||
            Legacy::IsValidTreasuryBlockHeight(blockHeight, validator.getChainParameters()) != validator.IsValidTreasuryBlockHeight(blockHeight))
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
        SuperblockSubsidyHeightValidator validator(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(validator);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        SuperblockSubsidyHeightValidator validator(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(validator);
    }
}

BOOST_AUTO_TEST_SUITE_END()
