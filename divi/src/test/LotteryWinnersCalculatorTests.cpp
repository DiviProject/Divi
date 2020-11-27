#include <spork.h>
#include <FakeBlockIndexChain.h>
#include <LotteryWinnersCalculator.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <script/script.h>
#include <vector>
#include <map>
#include <test/test_only.h>

#include <MockSuperblockHeightValidator.h>
#include <memory>
#include <random.h>

using ::testing::NiceMock;
using ::testing::_;
using ::testing::Invoke;

constexpr uint64_t unixTimestampForDec31stMidnight = 1609459199;
class LotteryWinnersCalculatorTestFixture
{
private:
    unsigned dummyScriptIndex_;
    unsigned lastUpdatedLotteryCoinstakesHeight_;
    std::unique_ptr<NiceMock<MockSuperblockHeightValidator>> heightValidator_;
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashes_;
    CSporkManager sporkManager_;
    int nextLockTime_;
public:
    const int lotteryStartBlock;
    std::unique_ptr<LotteryWinnersCalculator> calculator_;
    LotteryWinnersCalculatorTestFixture(
        ): dummyScriptIndex_(0u)
        , lastUpdatedLotteryCoinstakesHeight_(0)
        , heightValidator_(new NiceMock<MockSuperblockHeightValidator>)
        , fakeBlockIndexWithHashes_()
        , sporkManager_()
        , nextLockTime_(0)
        , lotteryStartBlock(100)
        , calculator_()
    {
    }

    void InitializeChainToFixedBlockCount(unsigned numberOfBlocks, unsigned blockTimeStart = unixTimestampForDec31stMidnight+1)
    {
        fakeBlockIndexWithHashes_.reset(new FakeBlockIndexWithHashes(numberOfBlocks,blockTimeStart,4));
        calculator_.reset(new LotteryWinnersCalculator(lotteryStartBlock,*fakeBlockIndexWithHashes_->activeChain,sporkManager_,*heightValidator_));
    }

    void SetDefaultLotteryStartAndCycleLength(int lotteryBlocksStart, int lotteryBlockCycleLength)
    {
        ON_CALL(*heightValidator_,IsValidLotteryBlockHeight(_)).WillByDefault(
            Invoke(
                [lotteryBlocksStart, lotteryBlockCycleLength](int blockHeight) -> bool {
                    return blockHeight >= lotteryBlocksStart && ((blockHeight-lotteryBlocksStart) % lotteryBlockCycleLength == 0);
                }
            )
            );
        ON_CALL(*heightValidator_,GetLotteryBlockPaymentCycle(_)).WillByDefault(
            Invoke(
                [lotteryBlocksStart, lotteryBlockCycleLength](int blockHeight) -> int {
                    return lotteryBlockCycleLength;
                }
            )
            );
    }

    CMutableTransaction createCoinstakeTxTransaction(CScript paymentScript)
    {
        CScript scriptEmpty;
        CAmount amount = 20000*COIN;
        CMutableTransaction tx;
        tx.nLockTime = nextLockTime_++;        // so all transactions get different hashes
        tx.vout.resize(2);
        tx.vout[0] = CTxOut(0, scriptEmpty);
        tx.vout[1] = CTxOut(amount, paymentScript);
        tx.vin.resize(1);
        {// Avoid flagging as a coinbase tx
            const uint256 randomHash = uint256S("4f5e1dcf6b28438ecb4f92c37f72bc430055fc91651f3dbc22050eb93164c579");
            constexpr uint32_t randomIndex = 42;
            tx.vin[0].prevout = COutPoint(randomHash,randomIndex);
        }
        assert(CTransaction(tx).IsCoinStake());
        return tx;
    }


    void SetCoinstakeAndUpdate(int blockHeight,CScript scriptPubKey)
    {
        CMutableTransaction tx;

        CBlockIndex* currentBlockIndex = fakeBlockIndexWithHashes_->activeChain->operator[](blockHeight);
        LotteryCoinstakeData previousCoinstakesData;


        if(currentBlockIndex->pprev) previousCoinstakesData = currentBlockIndex->pprev->vLotteryWinnersCoinstakes;
        currentBlockIndex->vLotteryWinnersCoinstakes =
            calculator_->CalculateUpdatedLotteryWinners(
                createCoinstakeTxTransaction(scriptPubKey), previousCoinstakesData, currentBlockIndex->nHeight);
    }

    void UpdateNextLotteryBlocks(unsigned numberOfLotteryBlocksToUpdate, CScript paymentScript)
    {
        while(numberOfLotteryBlocksToUpdate > 0)
        {
            SetCoinstakeAndUpdate(lastUpdatedLotteryCoinstakesHeight_,paymentScript);
            ++lastUpdatedLotteryCoinstakesHeight_;
            --numberOfLotteryBlocksToUpdate;
        }
    }

    const LotteryCoinstakes& getLotteryCoinstakes(int blockHeight) const
    {
        return fakeBlockIndexWithHashes_->activeChain->operator[](blockHeight)->vLotteryWinnersCoinstakes.getLotteryCoinstakes();
    }

    CScript constructDistinctDummyScript()
    {
        CScript dummyScript = CScript() << OP_TRUE;
        for(unsigned countOfDistinctDummyScripts = 0; countOfDistinctDummyScripts < dummyScriptIndex_; ++countOfDistinctDummyScripts)
        {
            dummyScript << OP_FALSE;
        }
        ++dummyScriptIndex_;
        return dummyScript;
    }
};

BOOST_FIXTURE_TEST_SUITE(LotteryWinnersCalculatorTests, LotteryWinnersCalculatorTestFixture)

BOOST_AUTO_TEST_CASE(willEnsureThatWinningALotteryForbidsWinningForTheNextThreeLotteries)
{
    SetDefaultLotteryStartAndCycleLength(100, 10);
    InitializeChainToFixedBlockCount(151);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(50,firstWinnerScript);

    BOOST_CHECK(getLotteryCoinstakes(110-1).size()>0);
    BOOST_CHECK(getLotteryCoinstakes(120-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(130-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(140-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(150-1).size()>0);
}

BOOST_AUTO_TEST_CASE(willEnsureThatBefore2021ThereAreNoVetosToRepeatedWinning)
{
    SetDefaultLotteryStartAndCycleLength(100, 20);
    InitializeChainToFixedBlockCount(301,unixTimestampForDec31stMidnight-60*201);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(200,firstWinnerScript);

    BOOST_CHECK_EQUAL(getLotteryCoinstakes(120-1).size(),11);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(140-1).size(),11);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(160-1).size(),11);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(180-1).size(),11);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(200-1).size(),11);

    BOOST_CHECK_EQUAL(getLotteryCoinstakes(220-1).size(),11);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(240-1).size(),0);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(260-1).size(),0);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(280-1).size(),0);
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(300-1).size(),11);
}

BOOST_AUTO_TEST_CASE(willAllowRepeatedWinnersOnlyIfNoNewWinnersAreAvailable)
{
    SetDefaultLotteryStartAndCycleLength(100, 50);
    InitializeChainToFixedBlockCount(151);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(40,firstWinnerScript);
    for(unsigned winnerCount = 0; winnerCount < 9; ++winnerCount)
    {
        UpdateNextLotteryBlocks(1,constructDistinctDummyScript());
    }
    BOOST_CHECK_EQUAL(getLotteryCoinstakes(150-1).size(),11u);
}
BOOST_AUTO_TEST_CASE(willRemoveLowestScoringDuplicateIfNewWinnersAreAvailable)
{
    SetDefaultLotteryStartAndCycleLength(100, 50);
    InitializeChainToFixedBlockCount(151);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(40,firstWinnerScript);
    const LotteryCoinstakes& singleWinnerCoinstakes = getLotteryCoinstakes(140);
    std::set<uint256> coinstakeTxHashesOfSingleWinnerCoinstakes;
    for(const LotteryCoinstake& coinstake: singleWinnerCoinstakes)
    {
        assert(coinstake.second == firstWinnerScript);
        coinstakeTxHashesOfSingleWinnerCoinstakes.insert(coinstake.first);
    }

    for(unsigned winnerCount = 0; winnerCount < 9; ++winnerCount)
    {
        UpdateNextLotteryBlocks(1,constructDistinctDummyScript());
        const LotteryCoinstakes& updatedCoinstakes = getLotteryCoinstakes(140+winnerCount+1);
        const LotteryCoinstake& coinstakeToRemove = *(singleWinnerCoinstakes.rbegin()+winnerCount);
        auto it = std::find_if(updatedCoinstakes.begin(),updatedCoinstakes.end(),
            [&coinstakeToRemove](const LotteryCoinstake& coinstake)
            {
                return coinstake.first == coinstakeToRemove.first &&
                        coinstake.second == coinstakeToRemove.second;
            });
        if(it != updatedCoinstakes.end())
        {
            BOOST_CHECK_MESSAGE(false,
                "Failed To Remove Lowest Scoring Coinstake: Expecting reverse index " +
                std::to_string(winnerCount) + " || Got reverse index: " + std::to_string( static_cast<int>(std::distance(it,updatedCoinstakes.end())-1))  );
            break;
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()