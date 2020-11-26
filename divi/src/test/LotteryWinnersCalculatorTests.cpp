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

    void InitializeChainToFixedBlockCount(unsigned numberOfBlocks)
    {
        fakeBlockIndexWithHashes_.reset(new FakeBlockIndexWithHashes(numberOfBlocks,0,4));
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

BOOST_AUTO_TEST_CASE(willEnsureThatASingleWinnerOfAllStakesCannotWinLotteryMoreThanOnceEveryLottery)
{
    SetDefaultLotteryStartAndCycleLength(100, 10);
    InitializeChainToFixedBlockCount(110);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(100,initialScript);
    CScript singleWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(10,singleWinnerScript);

    std::map<CScript,unsigned> paymentScripts;
    for(const auto& txHashAndPaymentScript: getLotteryCoinstakes(110-1))
    {
        ++paymentScripts[txHashAndPaymentScript.second];
    }
    BOOST_CHECK(paymentScripts[singleWinnerScript]==1);
}

BOOST_AUTO_TEST_CASE(willEnsureThatWinningALotteryTwiceCannotOccurByInterleavingWins)
{
    SetDefaultLotteryStartAndCycleLength(100, 10);
    InitializeChainToFixedBlockCount(111);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    CScript secondWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(1,firstWinnerScript);
    UpdateNextLotteryBlocks(1,secondWinnerScript);
    UpdateNextLotteryBlocks(1,firstWinnerScript);
    UpdateNextLotteryBlocks(7,secondWinnerScript);

    std::map<CScript,unsigned> paymentScripts;
    for(const auto& txHashAndPaymentScript: getLotteryCoinstakes(110-1))
    {
        ++paymentScripts[txHashAndPaymentScript.second];
    }
    BOOST_CHECK(paymentScripts[firstWinnerScript]==1);
    BOOST_CHECK(paymentScripts[secondWinnerScript]==1);
}

BOOST_AUTO_TEST_CASE(willEnsureThatWinningALotteryForbidsWinningForTheNextThreeLotteries)
{
    SetDefaultLotteryStartAndCycleLength(100, 10);
    InitializeChainToFixedBlockCount(151);

    CScript initialScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(101,initialScript);
    CScript firstWinnerScript = constructDistinctDummyScript();
    UpdateNextLotteryBlocks(50,firstWinnerScript);

    BOOST_CHECK(getLotteryCoinstakes(110-1).size()==1);
    BOOST_CHECK(getLotteryCoinstakes(120-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(130-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(140-1).size()==0);
    BOOST_CHECK(getLotteryCoinstakes(150-1).size()==1);
}

BOOST_AUTO_TEST_SUITE_END()