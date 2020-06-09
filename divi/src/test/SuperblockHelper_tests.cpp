#include <test/test_only.h>
#include <SuperblockHelpers.h>
#include <chainparams.h>
#include <MockSuperblockHeightValidator.h>
#include <MockBlockSubsidyProvider.h>
#include <memory>
#include <LegacyBlockSubsidies.h>
#include <SuperblockHeightValidator.h>
#include <BlockSubsidyProvider.h>

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

    CAmount GetTreasuryReward(int blockHeight, I_SuperblockHeightValidator& heightValidator, I_BlockSubsidyProvider& blockSubsidyProvider) const
    {
        CAmount totalReward = 0;
        if(blockHeight==0) return totalReward;
        if(heightValidator.IsValidTreasuryBlockHeight(blockHeight))
        {
            return blockSubsidyProvider.GetBlockSubsidity(blockHeight).nTreasuryReward;
        }
        return totalReward;
    }

    CAmount GetCharityReward(int blockHeight, I_SuperblockHeightValidator& heightValidator, I_BlockSubsidyProvider& blockSubsidyProvider) const
    {
        CAmount totalReward = 0;
        if(blockHeight==0) return totalReward;
        if(heightValidator.IsValidTreasuryBlockHeight(blockHeight))
        {
            return blockSubsidyProvider.GetBlockSubsidity(blockHeight).nCharityReward;
        }
        return totalReward;
    }

    CAmount GetLotteryReward(int blockHeight, I_SuperblockHeightValidator& heightValidator, I_BlockSubsidyProvider& blockSubsidyProvider) const
    {
        CAmount totalReward = 0;
        if(blockHeight==0) return totalReward;
        if(heightValidator.IsValidLotteryBlockHeight(blockHeight))
        {
            return blockSubsidyProvider.GetBlockSubsidity(blockHeight).nLotteryReward;
        }
        return totalReward;
    }

    SuperblockSubsidyProviderTestFixture(
        ): heightValidator_( new NiceMock<MockSuperblockHeightValidator>)
        , blockSubsidyProvider_( new NiceMock<MockBlockSubsidyProvider> )
    {
    }

    void setChainParameters(const CChainParams& chainParameters)
    {
    }
    void reset(const CChainParams& chainParameters)
    {
        heightValidator_.reset( new NiceMock<MockSuperblockHeightValidator>);
        blockSubsidyProvider_.reset( new NiceMock<MockBlockSubsidyProvider> );
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
            GetTreasuryReward(firstTreasuryBlockHeight,*heightValidator_, *blockSubsidyProvider_) == firstExpectedRewards,
            "Inconsistent rewards");
        
        CAmount secondExpectedRewards = secondBlockRewards.nTreasuryReward;
        BOOST_CHECK_MESSAGE(
            GetTreasuryReward(secondTreasuryBlockHeight,*heightValidator_, *blockSubsidyProvider_) == secondExpectedRewards,
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
                    GetTreasuryReward(treasuryBlockHeight,*heightValidator_, *blockSubsidyProvider_) == expectedRewards,
                    "Inconsistent rewards");
            }
        }
    }

    void zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(const CChainParams& chainParams)
    {
        ON_CALL(*heightValidator_,IsValidTreasuryBlockHeight(_))
            .WillByDefault(
                Return(false)
            );
        setChainParameters(chainParams);
        auto concreteBlockSubsidyProvider = std::make_shared<BlockSubsidyProvider>(chainParams,*heightValidator_);
        for(int blockHeight = 0; blockHeight < chainParams.GetTreasuryPaymentsCycle()*10; blockHeight++)
        {
            BOOST_CHECK_MESSAGE(
                concreteBlockSubsidyProvider->GetBlockSubsidity(blockHeight).nTreasuryReward == CAmount(0),
                "Inconsistent rewards");
        }
    }

    void checkBackwardCompatibility(const CChainParams& chainParams)
    {
        int transitionHeight =  SuperblockHeightValidator(chainParams).getTransitionHeight();
        auto concreteHeightValidator = std::make_shared<SuperblockHeightValidator>(chainParams);
        auto concreteBlockSubsidyProvider = std::make_shared<BlockSubsidyProvider>(chainParams,*concreteHeightValidator);

        for(int blockHeight =0; blockHeight < transitionHeight; blockHeight++)
        {
            if( concreteHeightValidator->IsValidTreasuryBlockHeight(blockHeight) )            
            {
                CAmount expectedTreasuryReward = Legacy::GetTreasuryReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualTreasuryReward = concreteBlockSubsidyProvider->GetBlockSubsidity(blockHeight).nTreasuryReward;

                BOOST_CHECK_MESSAGE(actualTreasuryReward == expectedTreasuryReward,
                    "Treasury: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualTreasuryReward << " vs. " << expectedTreasuryReward);
                if(actualTreasuryReward != expectedTreasuryReward) break;

                CAmount expectedCharityReward = Legacy::GetCharityReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualCharityReward = concreteBlockSubsidyProvider->GetBlockSubsidity(blockHeight).nCharityReward;

                BOOST_CHECK_MESSAGE(actualCharityReward == expectedCharityReward,
                    "Charity: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualCharityReward << " vs. " << expectedCharityReward);
                if(actualCharityReward != expectedTreasuryReward) break;
            }
            if( concreteHeightValidator->IsValidLotteryBlockHeight(blockHeight) )
            {
                CAmount expectedLotteryReward = Legacy::GetLotteryReward(Legacy::GetBlockSubsidity(blockHeight,chainParams),chainParams);
                CAmount actualLotteryReward = concreteBlockSubsidyProvider->GetBlockSubsidity(blockHeight).nLotteryReward;

                BOOST_CHECK_MESSAGE(actualLotteryReward == expectedLotteryReward,
                    "Lottery: Not backward compatible rewards! Height " << blockHeight 
                    << "! " << actualLotteryReward << " vs. " << expectedLotteryReward);
                if(actualLotteryReward != expectedLotteryReward) break;
            }
        }
    }

    void checkTreasuryBlockCycle(const CChainParams& chainParams)
    {
        auto concreteHeightValidator = std::make_shared<SuperblockHeightValidator>(chainParams);
        int lastKnownHeightOfTreasuryblock = 0;
        for(int blockHeight =0; blockHeight < concreteHeightValidator->getTransitionHeight()*2; blockHeight++)
        {
            if(concreteHeightValidator->IsValidTreasuryBlockHeight(blockHeight))
            {
                int actual =(blockHeight-lastKnownHeightOfTreasuryblock);
                int expected = concreteHeightValidator->GetTreasuryBlockPaymentCycle(blockHeight);
                bool testPass = actual == expected;
                BOOST_CHECK_MESSAGE(testPass, 
                    "Invalid treasury block cycle length at height " << blockHeight 
                    << "! " << actual << " vs. " << expected);
                lastKnownHeightOfTreasuryblock = blockHeight;
                if(!testPass) return;
            }
        }
    }

    void checkLotteryBlockCycle(const CChainParams& chainParams)
    {
        auto concreteHeightValidator = std::make_shared<SuperblockHeightValidator>(chainParams);
        int lastKnownHeightOfLotteryblock = 0;
        for(int blockHeight =0; blockHeight < concreteHeightValidator->getTransitionHeight()*2; blockHeight++)
        {
            if(concreteHeightValidator->IsValidLotteryBlockHeight(blockHeight))
            {
                int actual =(blockHeight-lastKnownHeightOfLotteryblock);
                int expected = concreteHeightValidator->GetLotteryBlockPaymentCycle(blockHeight);
                bool testPass = actual == expected;
                BOOST_CHECK_MESSAGE(testPass, 
                    "Invalid lottery block cycle length at height " << blockHeight 
                    << "! " << actual << " vs. " << expected);
                lastKnownHeightOfLotteryblock = blockHeight;
                if(!testPass) return;
            }
        }
    }

    template <typename Action>
    void checkSuperblockSubsidiesDoNotUndermintAfterHalving(
        const CChainParams& chainParams, 
        int superblockCycleLength,
        bool superblockIsTreasuryBlock,
        Action getReward)
    {
        reset(chainParams);

        int subsidyHalvingHeight =  2*chainParams.SubsidyHalvingInterval();
        int superblockAfterHalving = subsidyHalvingHeight - (subsidyHalvingHeight % superblockCycleLength) + superblockCycleLength;
        int superblockBeforeHalving = superblockAfterHalving - superblockCycleLength;

        CBlockRewards firstBlockRewards = Legacy::GetBlockSubsidity(superblockBeforeHalving,chainParams);
        CBlockRewards secondBlockRewards = Legacy::GetBlockSubsidity(superblockAfterHalving,chainParams);
        
        if(superblockIsTreasuryBlock)
        {
            ON_CALL(*heightValidator_,IsValidTreasuryBlockHeight(_))
            .WillByDefault(
                Invoke(
                    [superblockAfterHalving,superblockBeforeHalving](int nHeight)
                    {
                        return nHeight == superblockAfterHalving ||
                            nHeight == superblockBeforeHalving;
                    }
                )
            );
            ON_CALL(*heightValidator_,GetTreasuryBlockPaymentCycle(_))
                .WillByDefault(
                    Invoke(
                        [superblockCycleLength](int nHeight)
                        {
                            return superblockCycleLength;
                        }
                    )
                );
        }
        else
        {
            ON_CALL(*heightValidator_,IsValidLotteryBlockHeight(_))
            .WillByDefault(
                Invoke(
                    [superblockAfterHalving,superblockBeforeHalving](int nHeight)
                    {
                        return nHeight == superblockAfterHalving ||
                            nHeight == superblockBeforeHalving;
                    }
                )
            );
            ON_CALL(*heightValidator_,GetLotteryBlockPaymentCycle(_))
                .WillByDefault(
                    Invoke(
                        [superblockCycleLength](int nHeight)
                        {
                            return superblockCycleLength;
                        }
                    )
                );
        }
        CAmount expectedReward = (superblockAfterHalving-subsidyHalvingHeight)* getReward(secondBlockRewards) +
            (subsidyHalvingHeight-superblockBeforeHalving)* getReward(firstBlockRewards);
            
        auto concreteBlockSubsidyProvider = std::make_shared<BlockSubsidyProvider>(chainParams,*heightValidator_);
        CAmount actualReward = getReward(concreteBlockSubsidyProvider->GetBlockSubsidity(superblockAfterHalving));
        BOOST_CHECK_MESSAGE(
            expectedReward == actualReward,
            "Mismatched values "<< actualReward << " vs. " << expectedReward << "!"
            );
    }
};

BOOST_FIXTURE_TEST_SUITE(BlockSubsidyProviderTests,SuperblockSubsidyProviderTestFixture)
BOOST_AUTO_TEST_CASE(willHaveZeroTreasuryRewardsIfNoHeightIsAValidSuperblock)
{
   {
       zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(Params(CBaseChainParams::MAIN));
   }
   {
       zeroTreasuryRewardsIfNoHeightIsAValidSuperblock(Params(CBaseChainParams::TESTNET));    
   }
}
BOOST_AUTO_TEST_CASE(willComputeRewardsAsAMultipleOfBlockNumberWhenOnlyOneSuperblockIsAvailable)
{
    {
        computesRewardAsMultipleOfBlockNumber(Params(CBaseChainParams::MAIN));
    }
    {
        computesRewardAsMultipleOfBlockNumber(Params(CBaseChainParams::TESTNET));
    }
}
BOOST_AUTO_TEST_CASE(willComputeAccumulatedBlockRewardsBetweenValidSuperblocks)
{
    {
        computesAccumulatedBlockRewardBetweenValidSuperblocks(Params(CBaseChainParams::MAIN));
    }
    {
        computesAccumulatedBlockRewardBetweenValidSuperblocks(Params(CBaseChainParams::TESTNET));
    }
}

BOOST_AUTO_TEST_CASE(willHaveBackwardCompatibleRewards)
{
    {
        checkBackwardCompatibility(Params(CBaseChainParams::MAIN));
    }
    {
        checkBackwardCompatibility(Params(CBaseChainParams::TESTNET));
    }
}
BOOST_AUTO_TEST_CASE(willCorrectlyCountTheNumberOfBlocksBetweenSameTypeSuperblocks)
{
    {
        checkTreasuryBlockCycle(Params(CBaseChainParams::MAIN));
    }
    {
        checkTreasuryBlockCycle(Params(CBaseChainParams::TESTNET));
    }
    {
        checkLotteryBlockCycle(Params(CBaseChainParams::MAIN));
    }
    {
        checkLotteryBlockCycle(Params(CBaseChainParams::TESTNET));
    }
}

BOOST_AUTO_TEST_CASE(willNotMintLessCoinsThanRequired)
{
    {
        const CChainParams& chainParams =Params(CBaseChainParams::MAIN);
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetTreasuryPaymentsCycle(),
            true,
            [](const CBlockRewards& a){ return a.nTreasuryReward;});
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetTreasuryPaymentsCycle(),
            true,
            [](const CBlockRewards& a){ return a.nCharityReward;});
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetLotteryBlockCycle(),
            false,
            [](const CBlockRewards& a){ return a.nLotteryReward;});
    }
    {
        const CChainParams& chainParams =Params(CBaseChainParams::TESTNET);
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetTreasuryPaymentsCycle(),
            true,
            [](const CBlockRewards& a){ return a.nTreasuryReward;});
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetTreasuryPaymentsCycle(),
            true,
            [](const CBlockRewards& a){ return a.nCharityReward;});
        checkSuperblockSubsidiesDoNotUndermintAfterHalving(
            chainParams, 
            chainParams.GetLotteryBlockCycle(),
            false,
            [](const CBlockRewards& a){ return a.nLotteryReward;});
    }
}

BOOST_AUTO_TEST_SUITE_END()
