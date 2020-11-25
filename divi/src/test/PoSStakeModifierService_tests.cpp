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
#include <MockPoSStakeModifierService.h>

using ::testing::NiceMock;
using ::testing::Exactly;
using ::testing::Return;
using ::testing::_;
class PoSStakeModifierServiceTestFixture
{
private:
    std::unique_ptr<FakeBlockIndexWithHashes> fakeBlockIndexWithHashes_;
public:
    std::unique_ptr<NiceMock<MockPoSStakeModifierService>> mockStakeModifierService_;
    std::unique_ptr<PoSStakeModifierService> stakeModifierService_;
    const uint64_t genesisStakeModifier;
    const uint64_t firstBlockStakeModifier;
    uint256 someHash;
    PoSStakeModifierServiceTestFixture(
        ): fakeBlockIndexWithHashes_()
        , mockStakeModifierService_(new NiceMock<MockPoSStakeModifierService>)
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
                *mockStakeModifierService_,
                *(fakeBlockIndexWithHashes_->blockIndexByHash),
                *(fakeBlockIndexWithHashes_->activeChain)));

        if(numberOfBlocks>0) getActiveChain()[0]->SetStakeModifier(genesisStakeModifier, true);
        if(numberOfBlocks>1) getActiveChain()[1]->SetStakeModifier(firstBlockStakeModifier, true);
    }

    const CChain& getActiveChain() const
    {
        return *(fakeBlockIndexWithHashes_->activeChain);
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

BOOST_FIXTURE_TEST_SUITE(PoSStakeModifierServiceTests,PoSStakeModifierServiceTestFixture)

BOOST_AUTO_TEST_CASE(willFailWhenChainTipBlockHashIsUnknown)
{
    StakingData stakingData;

    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(stakingData);
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == 0);
}
BOOST_AUTO_TEST_CASE(willFailWhenChainTipBlockHashIsKnownButFirstConfirmationBlockHashIsUnknown)
{
    Init(200);
    StakingData stakingData;
    stakingData.blockHashOfFirstConfirmationBlock_  = 0;
    stakingData.blockHashOfChainTipBlock_  = getActiveChain().Tip()->GetBlockHash();

    std::pair<uint64_t,bool> stakeModifierQuery = stakeModifierService_->getStakeModifier(stakingData);
    BOOST_CHECK(!stakeModifierQuery.second);
    BOOST_CHECK(stakeModifierQuery.first == 0);
}
BOOST_AUTO_TEST_CASE(willNotQueryDecoratedStakeModifierServiceIfChainTipBlockHashIsUnknown)
{
    Init(200);
    StakingData stakingData;
    EXPECT_CALL(*mockStakeModifierService_,getStakeModifier(stakingData)).Times(Exactly(0));

    stakeModifierService_->getStakeModifier(stakingData);
}
BOOST_AUTO_TEST_CASE(willQueryDecoratedStakeModifierServiceIfChainTipBlockHashIsKnown)
{
    Init(200);
    StakingData stakingData;
    stakingData.blockHashOfChainTipBlock_  = getActiveChain().Tip()->GetBlockHash();
    EXPECT_CALL(*mockStakeModifierService_,getStakeModifier(stakingData)).Times(Exactly(1));

    stakeModifierService_->getStakeModifier(stakingData);
}

BOOST_AUTO_TEST_CASE(willDelegateToDecoradtedStakeModifierServiceBefore2021)
{
    typedef std::pair<uint64_t,bool> StakeModifierQueryResult;
    unsigned unixTimestampDecember31st2020Midnight = 1609459199;
    uint64_t defaultStakeModifier = 0x6e657779656172;
    StakeModifierQueryResult defaultStakeModifierQueryResult = {defaultStakeModifier, true};

    Init(100, unixTimestampDecember31st2020Midnight - 3600);
    ON_CALL(*mockStakeModifierService_,getStakeModifier(_)).WillByDefault(Return(defaultStakeModifierQueryResult));
    StakingData stakingData;
    CBlockIndex* currentBlockIndex = getActiveChain().Tip();
    while(currentBlockIndex)
    {
        if(currentBlockIndex->GetBlockTime() <= unixTimestampDecember31st2020Midnight)
        {
            stakingData.blockHashOfChainTipBlock_ = currentBlockIndex->GetBlockHash();
            StakeModifierQueryResult queryResult = stakeModifierService_->getStakeModifier(stakingData);
            if(queryResult.first != defaultStakeModifierQueryResult.first ||
               queryResult.second != defaultStakeModifierQueryResult.second)
            {
                BOOST_CHECK(false);
                break;
            }
        }

        currentBlockIndex = currentBlockIndex->pprev;
    }
}

BOOST_AUTO_TEST_CASE(willSwitchToGettingLastSetStakeModifierFromChainTipAfter2020)
{
    typedef std::pair<uint64_t,bool> StakeModifierQueryResult;
    unsigned unixTimestampDecember31st2020Midnight = 1609459199;
    uint64_t defaultStakeModifier = 0x6e657779656172;
    StakeModifierQueryResult defaultStakeModifierQueryResult = {defaultStakeModifier, true};

    Init(100, unixTimestampDecember31st2020Midnight);
    ON_CALL(*mockStakeModifierService_,getStakeModifier(_)).WillByDefault(Return(defaultStakeModifierQueryResult));
    StakingData stakingData;
    stakingData.blockHashOfChainTipBlock_  = getActiveChain().Tip()->GetBlockHash();


    StakeModifierQueryResult queryResult = stakeModifierService_->getStakeModifier(stakingData);
    uint64_t expectedStakeModifier = getLastStakeModifier(getActiveChain().Tip());
    BOOST_CHECK(queryResult.first == expectedStakeModifier);
    BOOST_CHECK(queryResult.second);
}

BOOST_AUTO_TEST_SUITE_END()