#include <test_only.h>
#include <PoSStakeModifierService.h>
#include <chain.h>
#include <blockmap.h>
#include <FakeBlockIndexChain.h>
#include <hash.h>
#include <utility>
#include <memory>
#include <StakeModifierIntervalHelpers.h>
#include <StakingData.h>

class TestSetup
{
private:
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashes_;
public:
    std::unique_ptr<PoSStakeModifierService> stakeModifierService_;
    const uint64_t genesisStakeModifier;
    const uint64_t firstBlockStakeModifier;
    uint256 someHash;
    TestSetup(
        ): fakeBlockIndexWithHashes_()
        , stakeModifierService_()
        , genesisStakeModifier(0)
        , firstBlockStakeModifier(0x12345)
        , someHash(uint256S("135bd924226929c2f4267f5e5c653d2a4ae0018187588dc1f016ceffe525fad2"))
    {
        Init();
    }
    void Init(
        unsigned numberOfBlocks=0,
        unsigned blockStartTime=0,
        unsigned versionNumber=4)
    {
        fakeBlockIndexWithHashes_.reset(
            new FakeBlockIndexWithHashes(
                numberOfBlocks,
                blockStartTime,
                versionNumber));
        stakeModifierService_.reset(
            new PoSStakeModifierService(
                *(fakeBlockIndexWithHashes_->blockIndexByHash),
                *(fakeBlockIndexWithHashes_->activeChain)));

        if(numberOfBlocks>0) getActiveChain()[0]->SetStakeModifier(genesisStakeModifier, true);
        if(numberOfBlocks>1) getActiveChain()[1]->SetStakeModifier(firstBlockStakeModifier, true);
    }

    const CChain& getActiveChain() const
    {
        return *(fakeBlockIndexWithHashes_->activeChain);
    }

    static StakingData fromBlockHash(const uint256& blockhash)
    {
        StakingData stakingData;
        stakingData.blockHashOfFirstConfirmationBlock_ = blockhash;
        return stakingData;
    }
    uint64_t getLastStakeModifier(CBlockIndex* currentIndex) const
    {
        assert(currentIndex);
        while(currentIndex && currentIndex->pprev && !currentIndex->GeneratedStakeModifier())
        {
            currentIndex = currentIndex->pprev;
        }
        assert(currentIndex->GeneratedStakeModifier());
        return currentIndex->nStakeModifier;
    }
    uint64_t getExpectedStakeModifier(CBlockIndex* currentIndex) const
    {
        assert(currentIndex);
        CHashWriter hasher(SER_GETHASH,0);
        hasher << currentIndex->GetBlockHash() << getLastStakeModifier(currentIndex);
        return hasher.GetHash().GetLow64();
    }
};

BOOST_FIXTURE_TEST_SUITE(PoSStakeModifierServiceTests,TestSetup)


BOOST_AUTO_TEST_CASE(willFailToGetValidStakeModifierOnAnEmptyChain)
{
    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(fromBlockHash(someHash));
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == uint64_t(0));
}

BOOST_AUTO_TEST_CASE(willFailToGetValidStakeModifierForAnUnknownHash)
{
    Init(200); // Initialize to 200 blocks;
    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(fromBlockHash(someHash));
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == uint64_t(0));
}

BOOST_AUTO_TEST_CASE(willReturnTheLower64bitsOfTheHashOfTheChainTipBlockHashAndTheLastGeneratedStakeModifier,SKIP_TEST)
{
    Init(200); // Initialize to 200 blocks;

    CBlockIndex* blockIndex = getActiveChain().Tip();
    uint64_t expectedStakeModifier = getExpectedStakeModifier(blockIndex);
    StakingData stakingData;
    stakingData.blockHashOfChainTipBlock_ = blockIndex->GetBlockHash();
    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(stakingData);
    BOOST_CHECK(stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == expectedStakeModifier);
}

BOOST_AUTO_TEST_SUITE_END()