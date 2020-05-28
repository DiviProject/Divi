#include <test/test_only.h>
#include <SuperblockHelpers.h>
#include <chainparams.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(SuperblockHelper_tests)


int getMaxBlockConflictHeight()
{
    int blockLotteryStart = Params().GetLotteryBlockStartBlock();
    int lotteryCycleLength = Params().GetLotteryBlockCycle();
    int blockTreasuryStart = Params().GetTreasuryPaymentsStartBlock();
    int treasuryCycleLength = Params().GetTreasuryPaymentsCycle();
    
    return 10*(lotteryCycleLength)*(treasuryCycleLength) + blockLotteryStart + blockTreasuryStart;
}
void CheckForBlockTypeConflicts()
{
    for(int blockHeight = 0; blockHeight <= getMaxBlockConflictHeight(); blockHeight++)
    {
        if(IsValidLotteryBlockHeight(blockHeight) && IsValidTreasuryBlockHeight(blockHeight))
        {
            BOOST_CHECK_MESSAGE(false, "A treasury block cannot also be a lottery block! Failure at height " << blockHeight << "!" );
            break;
        }
    }
}
BOOST_AUTO_TEST_CASE(willNeverAssertAHeightIsValidForBothTreasuryAndLottery)
{
    {
        SelectParams(CBaseChainParams::MAIN);
        CheckForBlockTypeConflicts();
    }
    {
        SelectParams(CBaseChainParams::TESTNET);
        CheckForBlockTypeConflicts();
    }
}

BOOST_AUTO_TEST_SUITE_END()
