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
#include <MockPoSStakeModifierService.h>
#include <sstream>

#include <gmock/gmock.h>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;

extern bool CreateHashProofForProofOfStake(
    const I_ProofOfStakeCalculator& calculator,
    const StakingData& stakingData,
    unsigned int& nTimeTx);

extern const int nHashDrift;

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

BOOST_AUTO_TEST_CASE(willEnsureBackwardCompatibilityWithMainnetHashproofs)
{
    struct PoSTestCase
    {
        StakingData stakingData;
        uint64_t stakeModifier;
        unsigned timestamp;
    };
    std::vector<PoSTestCase> PoSTestCases =
        {
            {//block 10k
                {
                    unsigned(470026099),
                    unsigned(1538645320),
                    uint256S("967b03e3c1daf39633ed73ffb29abfcab9ae5b384dc5b95dabee0890bf8b4546"),
                    COutPoint(uint256S("4266403b499375917920311b1af704805d3fa2d6d6f4e3217026618028423607"), 1),
                    CAmount(62542750000000),
                    uint256("acf49c06030a7a76059a25b174dc7adcdc5f4ad36c91b564c585743af4829f7a")
                },
                uint64_t(13260253192),
                unsigned(1538663336)
            },
            {// block 20k
                {
                    unsigned(469856526),
                    unsigned(1539089488),
                    uint256S("5cf4c9c2cf5b7ce030d39bb8b322f4b7f60bfe3408c48071e88cbe82d1b0c05e"),
                    COutPoint(uint256S("9e6dea178ac919781836e19fd773fda9788a59b3ff0fc7daddaccaa880f55396"),1),
                    CAmount(9647175000000),
                    uint256S("d4504de09669961a5d483070a0b4a0bd2231eda531b360ea5992c3cedaf096a0")
                },
                uint64_t(1384085403558),
                unsigned(1539268125)
            },
            {// block 30k
                {
                    unsigned(469821893),
                    unsigned(1539779674),
                    uint256S("854dfc8fca75d2ff38b1d7eaa3d049d14f5c2756ae9e385faeef7b616aebfcb8"),
                    COutPoint(uint256S("91d6a2ecdc9d7afeec1b9ac711046188ea15022a439a5ae4b95b8ce725e45144"),2),
                    CAmount(7536149207500),
                    uint256S("170781f60c8e5a7705a55d42272263b017df80d8c12a6eb7af6d74f5ba139cac")
                },
                uint64_t(32334572655),
                unsigned(1539874973)
            },
            {// block 720k
                {
                    unsigned(453551099),
                    unsigned(1580666056),
                    uint256S("dec4fd2e49eda04dae3bb36c4eea0c57f677a930bb7dcbea8d9957a90b11464c"),
                    COutPoint(uint256S("01dfa586a601481b66309b574bed91eccb0921c6faf908538b935e677a338b90"),1),
                    CAmount(9126800000000),
                    uint256S("d397d5a4e387246ba3bc1e8c22c0dcf7d00da3dc8e78e63648ee31de88e83b0c")
                },
                uint64_t(2770002504),
                unsigned(1581700773)
            },
            {// block 800k
                {
                    unsigned(453485185),
                    unsigned(1586034307),
                    uint256S("9cc08403bfeac16eb293b848d619639f52a693d13f00f0188c1efeb85fff332c"),
                    COutPoint(uint256S("47b3248311ada187ad01b25c4274289224fc16e185fa4427fa0dc62cde426913"),1),
                    CAmount(5846400000000),
                    uint256S("79808b1228f217c55ab46b52b7c5b120afeed598cb86bd8d7062bb6b1d90da4e")
                },
                uint64_t(1935523748),
                unsigned(1586553130)
            },
            {// block 1M
                {
                    unsigned(453338064),
                    unsigned(1598487374),
                    uint256S("e5fd3874ca56174d611c8925785a0dda728a4160b59ab777644e7a17500576d4"),
                    COutPoint(uint256S("d17d0226b20b1853b6ad50e73f132a1bd1ce1b5fa08db17c0cbbc93b82619da1"),1),
                    CAmount(1445296875000),
                    uint256S("25f7f482cbf34cd7da9d5db0e3b633c8c0abe54e0de1ef96e97ba15e8713e984")
                },
                uint64_t(3657064020262),
                unsigned(1598693544)
            },
            {// block 1.1M
                {
                    unsigned(453369958),
                    unsigned(1603300997),
                    uint256S("1f41f84a8aa151b900c66ba4c9423f7eef93bd076c030af54fa1dae1d238cfa0"),
                    COutPoint(uint256S("b5071db8b245780b77ecc6d4423ed5e8465b14065d19c394ec14ff0b5fde4d3a"),1),
                    CAmount(5557600000000),
                    uint256S("bd04a024f2f3ad91e694d4c70c6a983f1e73bb7803333d0406e9c314d6c265b7")
                },
                uint64_t(2109570660),
                unsigned(1604757745)
            },
            {// block 1.19M
                {
                    unsigned(453347746),
                    unsigned(1599860645),
                    uint256S("4d2597aa8ff30a0f0f82466e1dfd7603d8f928e08c8887597e0e0524ae293e5c"),
                    COutPoint(uint256S("1f5d59023369ac9f32d25de4645cd4a1d101911a01514a1a854a6518795f4805"),1),
                    CAmount(1699450000000),
                    uint256S("2dfcd9caa2558c41148a13b67117197289863a887480825646acf5225bcc0156")
                },
                uint64_t(11764753600649114182ull),
                unsigned(1610200830)
            }
        };

    unsigned testCaseIndex =0;
    for(const auto& testCase: PoSTestCases)
    {
        const StakingData& stakingData = testCase.stakingData;
        const uint64_t& stakeModifier = testCase.stakeModifier;
        const unsigned& timestamp = testCase.timestamp;
        MockPoSStakeModifierService mockPoSStakeModifiers;
        ON_CALL(mockPoSStakeModifiers,getStakeModifier(stakingData)).WillByDefault(Return(std::make_pair(stakeModifier,true) ));
        ProofOfStakeGenerator generator(mockPoSStakeModifiers,0);
        unsigned initialTimestamp = timestamp;
        BOOST_CHECK_MESSAGE(
            generator.CreateHashproofTimestamp(stakingData,initialTimestamp).succeeded(),
            "Failed to generate PoS - "+std::to_string(testCaseIndex));
        BOOST_CHECK_MESSAGE(initialTimestamp==timestamp,"MismatchedTimestamps "+std::to_string(testCaseIndex));
        ++testCaseIndex;
    }
}

BOOST_AUTO_TEST_SUITE_END()