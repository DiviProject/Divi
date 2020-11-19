#include <test_only.h>
#include <LegacyPoSStakeModifierService.h>
#include <chain.h>
#include <blockmap.h>
#include <FakeBlockIndexChain.h>
#include <hash.h>
#include <utility>
#include <memory>
#include <StakeModifierIntervalHelpers.h>
class FakeBlockIndexWithHashes
{
private:
    uint256 randomBlockHashSeed_;
    unsigned numberOfBlocks_;
    FakeBlockIndexChain fakeBlockIndexChain_;
public:
    BlockMap blockIndexByHash;
    CChain activeChain;
    FakeBlockIndexWithHashes(
        unsigned numberOfBlocks,
        unsigned blockStartTime,
        unsigned versionNumber
        ): randomBlockHashSeed_(uint256S("135bd924226929c2f4267f5e5c653d2a4ae0018187588dc1f016ceffe525fad2"))
        , numberOfBlocks_(numberOfBlocks)
        , fakeBlockIndexChain_()
        , blockIndexByHash()
        , activeChain()
    {
        for(unsigned blockHeight = 0; blockHeight < numberOfBlocks; ++blockHeight)
        {
            fakeBlockIndexChain_.extendBy(1,blockStartTime+60*blockHeight,versionNumber);
            CHashWriter hasher(SER_GETHASH,0);
            hasher << randomBlockHashSeed_ << blockHeight;
            BlockMap::iterator it = blockIndexByHash.insert(std::make_pair(hasher.GetHash(), fakeBlockIndexChain_.tip() )).first;
            fakeBlockIndexChain_.tip()->phashBlock = &(it->first);
        }
        activeChain.SetTip(fakeBlockIndexChain_.tip());
    }
};
class TestSetup
{
private:
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashes_;
public:
    std::unique_ptr<LegacyPoSStakeModifierService> stakeModifierService_;
    uint256 someHash;
    TestSetup(
        ): fakeBlockIndexWithHashes_()
        , stakeModifierService_()
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
            new LegacyPoSStakeModifierService(
                fakeBlockIndexWithHashes_->blockIndexByHash,
                fakeBlockIndexWithHashes_->activeChain));
    }

    const CChain& getActiveChain() const
    {
        return fakeBlockIndexWithHashes_->activeChain;
    }
};

BOOST_FIXTURE_TEST_SUITE(PoSStakeModifierServiceTests,TestSetup)

BOOST_AUTO_TEST_CASE(willFailToGetValidStakeModifierOnAnEmptyChain)
{
    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(someHash);
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == uint64_t(0));
}
BOOST_AUTO_TEST_CASE(willFailToGetValidStakeModifierForAnUnknownHash)
{
    Init(200); // Initialize to 200 blocks;
    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(someHash);
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == uint64_t(0));
}

BOOST_AUTO_TEST_CASE(willReturnStakeModifierOfZeroWhenAskedForTheChainTipsStakeModifierIfNotSet)
{
    Init(200); // Initialize to 200 blocks;
    CBlockIndex* chainTip = getActiveChain().Tip();
    assert(chainTip);
    {
        std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(chainTip->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK(stakeModifierQuery.first == uint64_t(0));
    }
}
BOOST_AUTO_TEST_CASE(willReturnStakeModifierForTheChainTipsStakeModifierWhenItsSet)
{
    Init(200); // Initialize to 200 blocks;
    CBlockIndex* chainTip = getActiveChain().Tip();
    assert(chainTip);
    {
        uint64_t stakeModifier = 0x26929c2;
        chainTip->SetStakeModifier(stakeModifier,true);
        std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(chainTip->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK(stakeModifierQuery.first == stakeModifier);
    }
}

BOOST_AUTO_TEST_CASE(willVerifyLengthOfSearchIntervalIs2087seconds)
{
    BOOST_CHECK_EQUAL(GetStakeModifierSelectionInterval(),2087);
}

BOOST_AUTO_TEST_CASE(willReturnStakeModifierForLastStakeModifierSetOrDefaultToZero)
{
    Init(200); // Initialize to 200 blocks;
    CBlockIndex* chainTip = getActiveChain().Tip();
    CBlockIndex* blockIndexOnePastStakeModifierSet = chainTip->GetAncestor(151);
    CBlockIndex* blockIndexWithStakeModifierSet = blockIndexOnePastStakeModifierSet->pprev;
    CBlockIndex* oldBlockIndex = blockIndexWithStakeModifierSet->GetAncestor(101);
    assert(chainTip);
    assert(blockIndexOnePastStakeModifierSet);
    assert(blockIndexWithStakeModifierSet);
    assert(oldBlockIndex);

    uint64_t stakeModifier = 0x26929c2;
    blockIndexWithStakeModifierSet->SetStakeModifier(stakeModifier,true);
    {
        std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(oldBlockIndex->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK_EQUAL(stakeModifierQuery.first, stakeModifier);

        stakeModifierQuery = stakeModifierService_->getStakeModifier(blockIndexOnePastStakeModifierSet->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK_EQUAL(stakeModifierQuery.first, uint64_t(0));
    }
}

BOOST_AUTO_TEST_CASE(willGetEarliestStakeModifierSetThatIsOutsideTheSelectionIntervalOrDefaultToTheChainTipIfSetAndZeroOtherwise)
{
    Init(200); // Initialize to 200 blocks;
    CBlockIndex* chainTip = getActiveChain().Tip();
    CBlockIndex* blockIndexWithStakeModifierSet = chainTip->GetAncestor(185);
    blockIndexWithStakeModifierSet->SetStakeModifier(blockIndexWithStakeModifierSet->nHeight,true);
    CBlockIndex* predecesor = blockIndexWithStakeModifierSet->pprev;
    while(blockIndexWithStakeModifierSet->GetBlockTime() -  predecesor->GetBlockTime() < GetStakeModifierSelectionInterval())
    {
        predecesor->SetStakeModifier(predecesor->nHeight,true);
        predecesor = predecesor->pprev;
    }

    assert(chainTip);
    assert(blockIndexWithStakeModifierSet);
    assert(predecesor);
    CBlockIndex* onePastPredecesor = chainTip->GetAncestor(predecesor->nHeight+1);

    {
        std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(predecesor->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK_EQUAL(stakeModifierQuery.first, uint64_t(blockIndexWithStakeModifierSet->nHeight) );

        stakeModifierQuery = stakeModifierService_->getStakeModifier(onePastPredecesor->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK_EQUAL(stakeModifierQuery.first, uint64_t(0));

        uint64_t stakeModifier = 0x26929c2;
        chainTip->SetStakeModifier(stakeModifier,true);
        stakeModifierQuery = stakeModifierService_->getStakeModifier(onePastPredecesor->GetBlockHash());
        BOOST_CHECK(stakeModifierQuery.second);
        BOOST_CHECK_EQUAL(stakeModifierQuery.first, stakeModifier);
    }
}

BOOST_AUTO_TEST_SUITE_END()