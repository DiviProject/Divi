#include <test_only.h>

#include <kernel.h>
#include <random.h>
#include <memory>
#include <amount.h>
#include <primitives/transaction.h>
#include <string>
#include <primitives/block.h>
#include <StakingData.h>
#include <ProofOfStakeCalculator.h>
#include <I_PoSStakeModifierService.h>
#include <ProofOfStakeGenerator.h>


#include <gmock/gmock.h>
using ::testing::Return;

extern bool CreateHashProofForProofOfStake(
    const I_ProofOfStakeCalculator& calculator,
    const StakingData& stakingData,
    unsigned int& nTimeTx);

extern bool CreateProofOfStakeCalculator(
    const I_PoSStakeModifierService& stakeModifierService,
    const StakingData& stakingData,
    const unsigned& initialHashproofTimestamp,
    std::shared_ptr<I_ProofOfStakeCalculator>& calculator);

extern const int nHashDrift;

class MockPoSStakeModifierService: public I_PoSStakeModifierService
{
public:
    typedef std::pair<uint64_t,bool> StakeModifierAndFoundStatusPair;
    MOCK_CONST_METHOD1(getStakeModifier, StakeModifierAndFoundStatusPair(const uint256&) );
};

BOOST_AUTO_TEST_SUITE(CreationOfHashproofs)

BOOST_AUTO_TEST_CASE(onlyHashesAFixedNumberOfTimesWhenDifficultyIsInfiniteDueToZeroValuedStake)
{
    CBlock blockHoldingUtxo;
    blockHoldingUtxo.nTime = GetRandInt(1<<20);
    unsigned chainTipDifficulty = 0x1000001;
    COutPoint utxo(GetRandHash(),GetRandInt(10));
    CAmount value = 0*COIN;
    unsigned transactionTimeStart = blockHoldingUtxo.nTime + 60*60*60;
    unsigned transactionTime = transactionTimeStart;
    MockPoSStakeModifierService stakeModifierService;
    ON_CALL(stakeModifierService,getStakeModifier).WillByDefault(Return(std::make_pair(0, true)));

    StakingData stakingData(
        chainTipDifficulty,
        blockHoldingUtxo.nTime,
        blockHoldingUtxo.GetHash(),
        utxo,
        value);

    std::shared_ptr<I_ProofOfStakeCalculator> calculator;
    assert(CreateProofOfStakeCalculator(stakeModifierService,stakingData,transactionTime,calculator));
    BOOST_CHECK_MESSAGE(
        !CreateHashProofForProofOfStake(
            *calculator,
            stakingData,
            transactionTime),
        "Proof of stake should not valid\n");

    BOOST_CHECK_MESSAGE( (transactionTimeStart-nHashDrift) == transactionTime,
        "Difference in the expected timestamps: "+std::to_string(transactionTimeStart-nHashDrift) + " vs. " +
        std::to_string(transactionTime)  );
}

BOOST_AUTO_TEST_CASE(willDetermineThatASuccessHashproofCreationResultSucceeded)
{
    BOOST_CHECK(HashproofCreationResult::Success(0).succeeded());
    BOOST_CHECK(HashproofCreationResult::Success(1).succeeded());
}
BOOST_AUTO_TEST_CASE(willDetermineThatASuccessHashproofCreationResultDidNotFailAtSetup)
{
    BOOST_CHECK(!HashproofCreationResult::Success(0).failedAtSetup());
    BOOST_CHECK(!HashproofCreationResult::Success(1).failedAtSetup());
}

BOOST_AUTO_TEST_CASE(willDetermineThatAFailedGenerationHashproofCreationResultCannotSuccedOrFailAtSetup)
{
    HashproofCreationResult result = HashproofCreationResult::FailedGeneration();
    BOOST_CHECK( !result.succeeded() && !result.failedAtSetup() );
}
BOOST_AUTO_TEST_CASE(willDetermineThatANonSuccessHashproofCreationResultDidNotSucceeded)
{
    BOOST_CHECK(!HashproofCreationResult::FailedSetup().succeeded());
    BOOST_CHECK(!HashproofCreationResult::FailedGeneration().succeeded());
}

BOOST_AUTO_TEST_CASE(willRecoverCorrectTimestampIfSuccessAndRecoverZeroOtherwise)
{
    uint32_t randomTimestamp = GetRandInt(100000);
    BOOST_CHECK(HashproofCreationResult::Success(randomTimestamp).timestamp() == randomTimestamp);
    BOOST_CHECK(HashproofCreationResult::FailedSetup().timestamp() == 0u);
    BOOST_CHECK(HashproofCreationResult::FailedGeneration().timestamp() == 0u);
}
BOOST_AUTO_TEST_SUITE_END()