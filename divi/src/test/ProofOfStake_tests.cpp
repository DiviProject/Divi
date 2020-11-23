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
#include <I_ProofOfStakeCalculator.h>

#include <gmock/gmock.h>
using ::testing::Return;
using ::testing::_;

extern bool CreateHashProofForProofOfStake(
    const I_ProofOfStakeCalculator& calculator,
    const StakingData& stakingData,
    unsigned int& nTimeTx);

extern const int nHashDrift;

class MockPoSStakeModifierService: public I_PoSStakeModifierService
{
public:
    typedef std::pair<uint64_t,bool> StakeModifierAndFoundStatusPair;
    MOCK_CONST_METHOD1(getStakeModifier, StakeModifierAndFoundStatusPair(const uint256&) );
};
class MockProofOfStakeCalculator: public I_ProofOfStakeCalculator
{
public:
    MOCK_CONST_METHOD3( computeProofOfStakeAndCheckItMeetsTarget, bool(unsigned int, uint256&, bool) );
};


BOOST_AUTO_TEST_SUITE(CreationOfHashproofs)

BOOST_AUTO_TEST_CASE(onlyHashesAFixedNumberOfTimesIfCalculatorRepeatedlyFails)
{
    uint32_t blockTimeOfFirstUTXOConfirmation = GetRandInt(1<<20);
    unsigned chainTipDifficulty = 0x1000001;
    unsigned transactionTimeStart = blockTimeOfFirstUTXOConfirmation + 60*60*60;
    unsigned transactionTime = transactionTimeStart;
    COutPoint utxo(GetRandHash(),GetRandInt(10));
    uint256 blockHash = GetRandHash();
    StakingData stakingData(chainTipDifficulty, blockTimeOfFirstUTXOConfirmation, blockHash, utxo, 0*COIN, 0);

    MockProofOfStakeCalculator calculator;
    ON_CALL(calculator, computeProofOfStakeAndCheckItMeetsTarget(_,_,_) ).WillByDefault(Return(false));
    BOOST_CHECK_MESSAGE(
        !CreateHashProofForProofOfStake(
            calculator,
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