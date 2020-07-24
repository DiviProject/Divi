#include <test_only.h>

#include <kernel.h>
#include <random.h>
#include <memory>
#include <amount.h>
#include <primitives/transaction.h>
#include <string>

extern uint256 stakeHash(uint64_t stakeModifier, unsigned int nTimeTx, const COutPoint& prevout, unsigned int nTimeBlockFrom);
extern bool stakeTargetHit(const uint256& hashProofOfStake, int64_t nValueIn, const uint256& bnTargetPerCoinDay, int64_t nTimeWeight);

class TestProofOfStakeFixture
{
private:
    static FastRandomContext context_;
public:
    std::shared_ptr<ProofOfStakeCalculator> calculator_;
    TestProofOfStakeFixture(
        ): calculator_()
    {
    }
    void setParameters(
        const COutPoint& utxoToStake,
        const int64_t& utxoValue,
        const uint64_t& stakeModifier,
        unsigned int blockDifficultyBits)
    {
        calculator_.reset(new ProofOfStakeCalculator(utxoToStake,utxoValue,stakeModifier,blockDifficultyBits));
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

    int64_t getCoinAgeWeight() const
    {
        return std::min<int64_t>(newerBlockTime - blockTimeForPreviousBlock, unsigned(60*60*7*24));
    }
    int64_t getNominalCoinAgeWeight() const
    {
        return std::min<int64_t>(newerBlockTime + timeOffsetForNewerBlock - blockTimeForPreviousBlock, unsigned(60*60*7*24));
    }

    void updateHashProof()
    {
        hashProof = stakeHash(stakeModifier,newerBlockTime,utxo,blockTimeForPreviousBlock);
    }

    bool oldCheck()
    {
        auto&& coinAgeWeightOfUtxo = getNominalCoinAgeWeight();
        uint256 targetPerCoinDay = uint256().SetCompact(difficulty);
        return stakeTargetHit(hashProof,amount,targetPerCoinDay, coinAgeWeightOfUtxo);
    }

    bool check()
    {
        auto&& coinAgeWeightOfUtxo = getCoinAgeWeight();
        uint256 targetPerCoinDay = uint256().SetCompact(difficulty);
        return stakeTargetHit(hashProof,amount,targetPerCoinDay, coinAgeWeightOfUtxo);
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
    ProofOfStake proof;
    unsigned currentTried = 0;
    unsigned maxTries = 10000;
    uint256 misleadingProofOfStake;

    while(!badProofOfStakeGenerated && currentTried < maxTries)
    {
        goodProofOfStakeGenerated = false;
        for( unsigned offset = 0; offset < numberOfHashAttempts; offset++ )
        {
            proof = ProofOfStake{utxo,amount,difficulty,blockTimeForPreviousBlock,newerBlockTime-offset,offset,stakeModifier,misleadingProofOfStake};
            proof.updateHashProof();
            if(proof.oldCheck())
            {
                goodProofOfStakeGenerated = true;
                if(!proof.check())
                {
                    badProofOfStakeGenerated = true;
                    break;
                }
            }
        }
        if(!badProofOfStakeGenerated )
        {
            ++newerBlockTime;
            ++blockTimeForPreviousBlock;
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

BOOST_AUTO_TEST_CASE(willNotCreateAnInvalidProofOfStake)
{
    bool successfulCreationOfBadProofOfStake = false;
    ProofOfStake proof = createBadProofOfStake(successfulCreationOfBadProofOfStake);
    BOOST_CHECK_MESSAGE(successfulCreationOfBadProofOfStake, "Failed to reproduce failure mode!");
    setParameters(
        proof.utxo,
        proof.amount,
        proof.stakeModifier,
        proof.difficulty);
    BOOST_CHECK_MESSAGE(
        calculator_->computeProofOfStakeAndCheckItMeetsTarget(
            proof.newerBlockTime,
            proof.blockTimeForPreviousBlock,
            proof.hashProof,
            true),
        "Proof-of-stake is not correct!"
        );
}

BOOST_AUTO_TEST_SUITE_END()