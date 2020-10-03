#include <test_only.h>

#include <kernel.h>
#include <random.h>
#include <memory>
#include <amount.h>
#include <primitives/transaction.h>
#include <string>
#include <primitives/block.h>

#include <gmock/gmock.h>
using ::testing::Return;

static constexpr unsigned int MaximumCoinAgeForStaking = 60 * 60 * 24 * 7 - 60 * 60;

extern bool CreateHashProofForProofOfStake(
    I_PoSStakeModifierService& stakeModifierService,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    unsigned int nBits,
    const CBlock& blockFrom,
    const COutPoint& prevout,
    const CAmount& utxoValue,
    unsigned int& nTimeTx,
    bool fCheck,
    uint256& hashProofOfStake);

extern uint256 stakeHash(uint64_t stakeModifier, unsigned int nTimeTx, const COutPoint& prevout, unsigned int nTimeBlockFrom);
extern bool stakeTargetHit(const uint256& hashProofOfStake, int64_t nValueIn, const uint256& bnTargetPerCoinDay, int64_t nTimeWeight);
extern const int nHashDrift;
class TestProofOfStakeFixture
{
private:
    static FastRandomContext context_;
public:
    std::shared_ptr<I_ProofOfStakeCalculator> calculator_;
    TestProofOfStakeFixture(
        ): calculator_()
    {
    }
    void setParametersNewCalculator(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits)
    {
        calculator_.reset(new ProofOfStakeCalculator(utxoToStake,utxoValue,stakeModifier,blockDifficultyBits));
    }
    void setParametersOldCalculator(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits,
        int64_t coinAgeWeight)
    {
        calculator_.reset(new LegacyProofOfStakeCalculator(utxoToStake,utxoValue,stakeModifier,blockDifficultyBits,coinAgeWeight));
    }

    int64_t getRandomValue() const
    {
        return static_cast<int64_t>(GetRand(100000));
    }
    COutPoint getRandomOutputPoint() const
    {
        return COutPoint(GetRandHash(),context_.rand32());
    }
    std::pair<COutPoint,int64_t> getRandomUtxoAndValuePair() const
    {
        return std::make_pair( getRandomOutputPoint(), getRandomValue() );
    }
};
FastRandomContext TestProofOfStakeFixture::context_(true);

struct ProofOfStake
{
    COutPoint utxo;
    CAmount amount;
    uint32_t difficulty;
    unsigned blockTimeForPreviousBlock;
    unsigned newerBlockTime;
    unsigned timeOffsetForNewerBlock;
    uint64_t stakeModifier;
    uint256 hashProof;

    std::shared_ptr<I_ProofOfStakeCalculator> oldCalculator_;
    std::shared_ptr<I_ProofOfStakeCalculator> newCalculator_;

    void resetCalculators()
    {
        int64_t coinAgeWeight = std::min<int64_t>(newerBlockTime - blockTimeForPreviousBlock, MaximumCoinAgeForStaking);
        oldCalculator_.reset(new LegacyProofOfStakeCalculator(utxo,amount,stakeModifier,difficulty,coinAgeWeight));
        newCalculator_.reset(new ProofOfStakeCalculator(utxo,amount,stakeModifier,difficulty));
    }

    void updateHashProof()
    {
        hashProof = stakeHash(stakeModifier,newerBlockTime-timeOffsetForNewerBlock,utxo,blockTimeForPreviousBlock);
    }

    bool oldCheck()
    {
        return oldCalculator_->computeProofOfStakeAndCheckItMeetsTarget(newerBlockTime-timeOffsetForNewerBlock,blockTimeForPreviousBlock,hashProof,true);
    }

    bool check()
    {
       return newCalculator_->computeProofOfStakeAndCheckItMeetsTarget(newerBlockTime-timeOffsetForNewerBlock,blockTimeForPreviousBlock,hashProof,true);
    }
};

ProofOfStake createBadProofOfStake(bool& succeded)
{
    COutPoint utxo = COutPoint(uint256S("0x4e009fadd2d925e62bc32f3556630b00558f37a0896038aa81961fc0aa7930e4"),0);
    CAmount amount = 400*COIN;
    uint32_t difficulty = 0x1f001001;
    unsigned blockTimeForPreviousBlock = 1595527000;
    unsigned newerBlockTime =            1595530700;
    uint64_t stakeModifier = uint256S("0xc82c1fd2fd243fee99de9fc36e77f309d34d3e24b6e8ac84b4d00009be05ae45").Get64();
    unsigned numberOfHashAttempts = 45;

    bool badProofOfStakeGenerated = false;
    bool goodProofOfStakeGenerated = false;
    ProofOfStake proof, contrastingProof;
    unsigned currentTried = 0;
    unsigned maxTries = 100000;
    uint256 misleadingProofOfStake;

    while(!badProofOfStakeGenerated && currentTried < maxTries)
    {
        goodProofOfStakeGenerated = false;
        for( unsigned offset = 0; offset < numberOfHashAttempts; offset++ )
        {
            proof = ProofOfStake{utxo,amount,difficulty,blockTimeForPreviousBlock,newerBlockTime,offset,stakeModifier,misleadingProofOfStake,{},{}};
            proof.updateHashProof();
            proof.resetCalculators();
            if(proof.oldCheck())
            {
                contrastingProof = ProofOfStake{utxo,amount,difficulty,blockTimeForPreviousBlock,newerBlockTime-offset,0,stakeModifier,misleadingProofOfStake,{},{}};
                contrastingProof.updateHashProof();
                contrastingProof.resetCalculators();
                goodProofOfStakeGenerated = true;
                if(contrastingProof.oldCheck() && !proof.check())
                {
                    badProofOfStakeGenerated = true;
                    proof = contrastingProof;
                    break;
                }
            }
        }
        if(!badProofOfStakeGenerated )
        {
            newerBlockTime += numberOfHashAttempts;
            currentTried += goodProofOfStakeGenerated;
        }
        else
        {
            succeded = true;
        }
    }

    return proof;
}

BOOST_FIXTURE_TEST_SUITE(ProofOfStakeTests,TestProofOfStakeFixture)

BOOST_AUTO_TEST_CASE(willNotCreateAnInvalidProofOfStake, SKIP_TEST)
{
    bool successfulCreationOfBadProofOfStake = false;
    ProofOfStake proof = createBadProofOfStake(successfulCreationOfBadProofOfStake);
    if(successfulCreationOfBadProofOfStake)
    {
        setParametersOldCalculator(
            proof.utxo,
            proof.amount,
            proof.stakeModifier,
            proof.difficulty,
            std::min<int64_t>(proof.newerBlockTime - proof.blockTimeForPreviousBlock, MaximumCoinAgeForStaking )
            );
        BOOST_CHECK_MESSAGE(
            calculator_->computeProofOfStakeAndCheckItMeetsTarget(
                proof.newerBlockTime,
                proof.blockTimeForPreviousBlock,
                proof.hashProof,
                true),
            "Proof-of-stake is not correct!"
            );
        setParametersNewCalculator(
            proof.utxo,
            proof.amount,
            proof.stakeModifier,
            proof.difficulty);
        BOOST_CHECK_MESSAGE(
            !calculator_->computeProofOfStakeAndCheckItMeetsTarget(
                proof.newerBlockTime,
                proof.blockTimeForPreviousBlock,
                proof.hashProof,
                true),
            "Proof-of-stake is not correct!"
            );
        BOOST_CHECK_MESSAGE(false,"Failed at creating backward compatible proof of stake!");
    }
}

BOOST_AUTO_TEST_SUITE_END()


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
    std::map<unsigned int, unsigned int> hashedBlockTimestamps;
    COutPoint utxo(GetRandHash(),GetRandInt(10));
    CAmount value = 0*COIN;
    unsigned transactionTimeStart = blockHoldingUtxo.nTime + 60*60*60;
    unsigned transactionTime = transactionTimeStart;
    uint256 hashProofOfStake;
    MockPoSStakeModifierService stakeModifierService;
    ON_CALL(stakeModifierService,getStakeModifier).WillByDefault(Return(std::make_pair(0, true)));

    BOOST_CHECK_MESSAGE(
        !CreateHashProofForProofOfStake(
            stakeModifierService,
            hashedBlockTimestamps,
            chainTipDifficulty,
            blockHoldingUtxo,
            utxo,
            value,
            transactionTime,
            false,
            hashProofOfStake),
        "Proof of stake should not valid\n");

    BOOST_CHECK_MESSAGE( (transactionTimeStart-nHashDrift) == transactionTime,
        "Difference in the expected timestamps: "+std::to_string(transactionTimeStart-nHashDrift) + " vs. " +
        std::to_string(transactionTime)  );
}

BOOST_AUTO_TEST_SUITE_END()