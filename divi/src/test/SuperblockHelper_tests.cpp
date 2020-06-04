#include <test/test_only.h>
#include <SuperblockHelpers.h>
#include <chainparams.h>
#include <MockSuperblockHeightValidator.h>
#include <MockBlockSubsidyProvider.h>
#include <memory>

#include <boost/test/unit_test.hpp>

using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;
using ::testing::Ref;

BOOST_AUTO_TEST_SUITE(SuperblockHelper_tests)

int getMaxBlockConflictHeight(CChainParams& chainParams)
{
    int blockLotteryStart = chainParams.GetLotteryBlockStartBlock();
    int lotteryCycleLength = chainParams.GetLotteryBlockCycle();
    int blockTreasuryStart = chainParams.GetTreasuryPaymentsStartBlock();
    int treasuryCycleLength = chainParams.GetTreasuryPaymentsCycle();
    
    return 10*(lotteryCycleLength)*(treasuryCycleLength) + blockLotteryStart + blockTreasuryStart;
}

void CheckForBlockTypeConflicts(CChainParams& chainParams,SuperblockHeightValidator& validator)
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
        SuperblockHeightValidator validator(chainParams);
        CheckForBlockTypeConflicts(chainParams, validator);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        SuperblockHeightValidator validator(chainParams);
        CheckForBlockTypeConflicts(chainParams, validator);
    }
}

void checkBackwardCompatibilityOfSuperblockValidity(SuperblockHeightValidator& validator)
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
        SuperblockHeightValidator validator(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(validator);
    }
    {
        CChainParams& chainParams = Params(CBaseChainParams::TESTNET);
        SuperblockHeightValidator validator(chainParams);
        checkBackwardCompatibilityOfSuperblockValidity(validator);
    }
}
BOOST_AUTO_TEST_SUITE_END()


class SuperblockSubsidyProviderTestFixture
{
public:
    std::shared_ptr<MockSuperblockHeightValidator> heightValidator_;
    std::shared_ptr<MockBlockSubsidyProvider> blockSubsidyProvider_;
    std::shared_ptr<SuperblockSubsidyProvider> superblockSubsidyProvider_;

    SuperblockSubsidyProviderTestFixture(
        ): heightValidator_( new NiceMock<MockSuperblockHeightValidator>)
        , blockSubsidyProvider_( new NiceMock<MockBlockSubsidyProvider> )
        , superblockSubsidyProvider_(NULL)
    {
    }

    void setChainParameters(const CChainParams& chainParameters)
    {
        superblockSubsidyProvider_ = 
            std::make_shared<SuperblockSubsidyProvider>(
                chainParameters,
                *heightValidator_,
                *blockSubsidyProvider_
            );
    }

    void computesAccumulatedBlockRewardBetweenValidSuperblocks(const CChainParams& chainParams)
    {
        int firstTreasuryBlockHeight = 100;
        int secondTreasuryBlockHeight = firstTreasuryBlockHeight+100;
        CBlockRewards firstBlockRewards(3,5,7,11,13,17);
        CBlockRewards secondBlockRewards(23,23,23,23,23,23);
        ON_CALL(*blockSubsidyProvider_, GetBlockSubsidity(_))
            .WillByDefault(
                Invoke(
                    [firstBlockRewards,secondBlockRewards,firstTreasuryBlockHeight,secondTreasuryBlockHeight](int nHeight)
                    {
                        if(nHeight == firstTreasuryBlockHeight)
                        {
                            return firstBlockRewards;
                        }
                        else if(nHeight == secondTreasuryBlockHeight)
                        {
                            return secondBlockRewards;
                        }
                        return CBlockRewards(0,0,0,0,0,0);
                    }
                )
            );
        ON_CALL(*heightValidator_,IsValidTreasuryBlockHeight(_))
            .WillByDefault(
                Invoke(
                    [firstTreasuryBlockHeight,secondTreasuryBlockHeight](int nHeight)
                    {
                        return nHeight == firstTreasuryBlockHeight ||
                            nHeight == secondTreasuryBlockHeight;
                    }
                )
            );

        setChainParameters(chainParams);
        CAmount firstExpectedRewards = firstBlockRewards.nTreasuryReward;
        BOOST_CHECK_MESSAGE(
            superblockSubsidyProvider_->GetTreasuryReward(firstTreasuryBlockHeight) == firstExpectedRewards,
            "Inconsistent rewards");
        
        CAmount secondExpectedRewards = secondBlockRewards.nTreasuryReward;
        BOOST_CHECK_MESSAGE(
            superblockSubsidyProvider_->GetTreasuryReward(secondTreasuryBlockHeight) == secondExpectedRewards,
            "Inconsistent rewards");
    }

    void computesRewardAsMultipleOfBlockNumber(const CChainParams& chainParams)
    {
        for(int treasuryBlockHeight = 0; treasuryBlockHeight < 100; treasuryBlockHeight++)
        {
            {
                CBlockRewards blockRewards(3,5,7,11,13,17);
                ON_CALL(*blockSubsidyProvider_, GetBlockSubsidity(_))
                    .WillByDefault(
                        Invoke(
                            [blockRewards](int nHeight)
                            {
                                *const_cast<CAmount*>(&blockRewards.nTreasuryReward) *= nHeight;
                                return blockRewards;
                            }
                        )
                    );
                ON_CALL(*heightValidator_,IsValidTreasuryBlockHeight(_))
                    .WillByDefault(
                        Invoke(
                            [treasuryBlockHeight](int nHeight)
                            {
                                return nHeight == treasuryBlockHeight;
                            }
                        )
                    );
                setChainParameters(chainParams);
                CAmount expectedRewards = treasuryBlockHeight*blockRewards.nTreasuryReward;

                BOOST_CHECK_MESSAGE(
                    superblockSubsidyProvider_->GetTreasuryReward(treasuryBlockHeight) == expectedRewards,
                    "Inconsistent rewards");
            }
        }
    }

    void zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(const CChainParams& chainParams)
    {
        CBlockRewards fixedBlockRewards(100,100,100,100,100,100);
        ON_CALL(*blockSubsidyProvider_, GetBlockSubsidity(_))
            .WillByDefault(
                Invoke(
                    [fixedBlockRewards](int nHeight)
                    {
                        return fixedBlockRewards;
                    }
                )
            );
        ON_CALL(*heightValidator_,IsValidTreasuryBlockHeight(_))
            .WillByDefault(
                Return(false)
            );
        setChainParameters(chainParams);
        for(int blockHeight = 0; blockHeight < 10000; blockHeight++)
        {
            BOOST_CHECK_MESSAGE(
                superblockSubsidyProvider_->GetTreasuryReward(blockHeight) == CAmount(0),
                "Inconsistent rewards");
        }
    }

    void checkBackwardCompatibility(const CChainParams& chainParams)
    {
        int transitionHeight =  SuperblockHeightValidator(chainParams).getTransitionHeight();
        auto concreteHeightValidator = std::make_shared<SuperblockHeightValidator>(chainParams);
        auto concreteBlockSubsidyProvider = std::make_shared<BlockSubsidyProvider>(chainParams,*concreteHeightValidator);
        superblockSubsidyProvider_ = 
            std::make_shared<SuperblockSubsidyProvider>(
                chainParams,
                *concreteHeightValidator,
                *concreteBlockSubsidyProvider
            );

        for(int blockHeight =0; blockHeight < transitionHeight; blockHeight++)
        {
            if( concreteHeightValidator->IsValidTreasuryBlockHeight(blockHeight) )            
            {
                CAmount expectedTreasuryReward = Legacy::GetTreasuryReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualTreasuryReward = superblockSubsidyProvider_->GetTreasuryReward(blockHeight);

                BOOST_CHECK_MESSAGE(actualTreasuryReward == expectedTreasuryReward,
                    "Treasury: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualTreasuryReward << " vs. " << expectedTreasuryReward);
                if(actualTreasuryReward != expectedTreasuryReward) break;

                CAmount expectedCharityReward = Legacy::GetCharityReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualCharityReward = superblockSubsidyProvider_->GetCharityReward(blockHeight);

                BOOST_CHECK_MESSAGE(actualCharityReward == expectedCharityReward,
                    "Charity: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualCharityReward << " vs. " << expectedCharityReward);
                if(actualCharityReward != expectedTreasuryReward) break;
            }
            if( concreteHeightValidator->IsValidLotteryBlockHeight(blockHeight) )
            {
                CAmount expectedLotteryReward = Legacy::GetLotteryReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualLotteryReward = superblockSubsidyProvider_->GetLotteryReward(blockHeight);

                BOOST_CHECK_MESSAGE(actualLotteryReward == expectedLotteryReward,
                    "Lottery: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualLotteryReward << " vs. " << expectedLotteryReward);
                if(actualLotteryReward != expectedLotteryReward) break;
            }
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(BlockSubsidyProviderTests,SuperblockSubsidyProviderTestFixture)
BOOST_AUTO_TEST_CASE(willHaveZeroTreasuryRewardsIfNoHeightIsAValidSuperblock)
{
   zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(Params(CBaseChainParams::MAIN));
   zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(Params(CBaseChainParams::TESTNET));    
}
BOOST_AUTO_TEST_CASE(willComputeRewardsAsAMultipleOfBlockNumberWhenOnlyOneSuperblockIsAvailable)
{
    computesRewardAsMultipleOfBlockNumber(Params(CBaseChainParams::MAIN));
    computesRewardAsMultipleOfBlockNumber(Params(CBaseChainParams::TESTNET));
}
BOOST_AUTO_TEST_CASE(willComputeAccumulatedBlockRewardsBetweenValidSuperblocks)
{
    
    computesAccumulatedBlockRewardBetweenValidSuperblocks(Params(CBaseChainParams::MAIN));
    computesAccumulatedBlockRewardBetweenValidSuperblocks(Params(CBaseChainParams::TESTNET));
}

BOOST_AUTO_TEST_CASE(willHaveBackwardCompatibleRewards)
{
    checkBackwardCompatibility(Params(CBaseChainParams::MAIN));
    checkBackwardCompatibility(Params(CBaseChainParams::TESTNET));
}

BOOST_AUTO_TEST_SUITE_END()
