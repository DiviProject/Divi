#include <test_only.h>

#include <CachedBIP9ActivationStateTracker.h>
#include <chain.h>
#include <test/FakeBlockIndexChain.h>
#include <ThresholdConditionCache.h>

static inline BIP9Deployment createViableBipDeployment()
{
    return BIP9Deployment(
            "ViableTestBIP9",
            0u,
            (int64_t)1588600341,
            (int64_t)1598600341,
            1000,
            900
            );
}
static inline BIP9Deployment createTimedOutBipDeployment()
{
    return BIP9Deployment(
        "TimedoutTestBIP9",
        0u,
        (int64_t)1598600341,
        (int64_t)1588600341,
        1000,
        900
        );
}
static inline BIP9Deployment createBipDeploymentWithPeriodLessThanThreshold()
{
    return BIP9Deployment(
        "UnattainableTestBIP9",
        0u,
        (int64_t)1598600341,
        (int64_t)1588600341,
        900,
        1000
        );
}


BOOST_AUTO_TEST_SUITE(CachedBIP9ActivationStateTracker_tests)

BOOST_AUTO_TEST_CASE(initial_test)
{
    BIP9Deployment bip;
    ThresholdConditionCache cache;
    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);
}
BOOST_AUTO_TEST_CASE(willFindStateToBeDefinedByDefaultIfBipIsViable)
{
    {
        BIP9Deployment bip;
        ThresholdConditionCache cache;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(NULL)==ThresholdState::FAILED);
    }
    {
        BIP9Deployment bip = createTimedOutBipDeployment();
        ThresholdConditionCache cache;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(NULL)==ThresholdState::FAILED);
    }
    {
        BIP9Deployment bip = createBipDeploymentWithPeriodLessThanThreshold();
        ThresholdConditionCache cache;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(NULL)==ThresholdState::FAILED);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(NULL)==ThresholdState::DEFINED);
    }
}

BOOST_AUTO_TEST_CASE(willFindStateInCacheIfPresent)
{
    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    std::shared_ptr<CBlockIndex> blockIndexPtr = std::make_shared<CBlockIndex>();
    assert(blockIndexPtr->nHeight % bip.nPeriod == 0);
    cache[blockIndexPtr.get()] = ThresholdState::ACTIVE;

    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(blockIndexPtr.get())==ThresholdState::ACTIVE);
}

BOOST_AUTO_TEST_CASE(willDeferToCachedStateAtApropriateHeight)
{

    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    int fakeChainSize = 2*bip.nPeriod;
    fakeChain.extendBy(fakeChainSize, 0, 0);
    cache[fakeChain.at(bip.nPeriod)] = ThresholdState::ACTIVE;

    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    for(int height = 0; height < bip.nPeriod; height++)
    {
        BOOST_CHECK_MESSAGE(
            activationStateTracker
                .getLastCachedStatePriorToBlockIndex(fakeChain.at(height))==ThresholdState::DEFINED,
            "The height is" << height);
    }
    for(int height = bip.nPeriod; height < fakeChainSize; height++)
    {
        BOOST_CHECK_MESSAGE(
            activationStateTracker
                .getLastCachedStatePriorToBlockIndex(fakeChain.at(height))==ThresholdState::ACTIVE,
            "The height is" << height);
    }
}

BOOST_AUTO_TEST_CASE(willDeferToCachedStateInMostRecentStartingBlockIfMedianBlockTimesAreNotLessThanStartTime)
{

    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    int fakeChainSize = 5*bip.nPeriod;
    fakeChain.extendBy(fakeChainSize, bip.nStartTime, 0);
    cache[fakeChain.at(bip.nPeriod)] = ThresholdState::FAILED;
    cache[fakeChain.at(4*bip.nPeriod)] = ThresholdState::ACTIVE;

    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    for(int height = 0; height < bip.nPeriod; height++)
    {
        BOOST_CHECK_MESSAGE(
            activationStateTracker
                .getLastCachedStatePriorToBlockIndex(fakeChain.at(height))==ThresholdState::DEFINED,
            "The height is" << height);
    }
    for(int height = bip.nPeriod; height < 4*bip.nPeriod; height++)
    {
        BOOST_CHECK_MESSAGE(
            activationStateTracker
                .getLastCachedStatePriorToBlockIndex(fakeChain.at(height))==ThresholdState::FAILED,
            "The height is" << height);
    }
    for(int height = 4*bip.nPeriod; height < fakeChainSize; height++)
    {
        BOOST_CHECK_MESSAGE(
            activationStateTracker
                .getLastCachedStatePriorToBlockIndex(fakeChain.at(height))==ThresholdState::ACTIVE,
            "The height is" << height);
    }
}

BOOST_AUTO_TEST_CASE(willNotChangeStateIfMedianBlockTimesArentMonotoneIncreasing)
{
    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    fakeChain.extendBy(bip.nPeriod, bip.nStartTime - 1, 0); // Stays In Defined
    fakeChain.extendBy(bip.nPeriod, bip.nStartTime, 0); // Moves to started
    fakeChain.extendBy(bip.nPeriod, bip.nStartTime,  VERSIONBITS_TOP_BITS | ( (int32_t)1 << bip.bit) ); // Moves To LOCKED_IN
    fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime,  0); // Moves To ACTIVE
    activationStateTracker.update(fakeChain.Tip());

    fakeChain.extendBy(1+CBlockIndex::nMedianTimeSpan, bip.nStartTime-1, 0); // Attemps to reverse BIP9 by publishing old blocks
    activationStateTracker.update(fakeChain.Tip());

    fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Reverts To Defined
    activationStateTracker.update(fakeChain.Tip());

    ThresholdState result = activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.Tip());
    BOOST_CHECK_MESSAGE(
        result == ThresholdState::ACTIVE,
        "Actual: " << static_cast<int>(result) << " vs. Expected " << static_cast<int>(ThresholdState::ACTIVE)
    );
}

BOOST_AUTO_TEST_CASE(willKeepAMinimalCacheOfState)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        fakeChain.extendBy(bip.nPeriod, bip.nStartTime - 1, 0); // Stays In Defined
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime, 0); // Moves to started
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime, 0); // Moves to started
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime, 0); // Moves to started
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime, 0); // Moves to started
        activationStateTracker.update(fakeChain.Tip());

        BOOST_CHECK(cache.size()<=3);
    }
    {
        BIP9Deployment bip = createTimedOutBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Moves to failed
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Moves to failed
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Moves to failed
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Moves to failed
        activationStateTracker.update(fakeChain.Tip());
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime+1, 0); // Moves to failed
        activationStateTracker.update(fakeChain.Tip());

        BOOST_CHECK(cache.size()<=3);
    }
}

BOOST_AUTO_TEST_CASE(willDetectBlockSignalsForBip)
{
    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    int32_t bipMask = ( (int32_t)1 << bip.bit);


    fakeChain.extendBy(1, 0, VERSIONBITS_TOP_BITS );
    fakeChain.extendBy(1, 0,  VERSIONBITS_TOP_BITS | bipMask );
    fakeChain.extendBy(1, 0, VERSIONBITS_TOP_BITS | ( bipMask << 1 ) );
    fakeChain.extendBy(1, 0, VERSIONBITS_TOP_BITS | ( bipMask << 1 ) | bipMask );

    BOOST_CHECK(!activationStateTracker.bipIsSignaledFor(fakeChain.at(0)));
    BOOST_CHECK(activationStateTracker.bipIsSignaledFor(fakeChain.at(1)));
    BOOST_CHECK(!activationStateTracker.bipIsSignaledFor(fakeChain.at(2)));
    BOOST_CHECK(activationStateTracker.bipIsSignaledFor(fakeChain.at(3)));
}


BOOST_AUTO_TEST_CASE(willClaimToUpdateIfBlocksMeetThresholdAndPriorStateIsStarted)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.threshold, bip.nStartTime, VERSIONBITS_TOP_BITS | (1 << bip.bit) );
        fakeChain.extendBy(bip.nPeriod+1 - bip.threshold, bip.nStartTime, 0);
        cache[fakeChain.at(0)] = ThresholdState::STARTED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::LOCKED_IN);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.threshold-1, bip.nStartTime, VERSIONBITS_TOP_BITS | (1 << bip.bit) );
        fakeChain.extendBy(bip.nPeriod+2 - bip.threshold, bip.nStartTime, 0);
        cache[fakeChain.at(0)] = ThresholdState::STARTED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))!=ThresholdState::LOCKED_IN);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.threshold, bip.nStartTime, VERSIONBITS_TOP_BITS | (1 << bip.bit) );
        fakeChain.extendBy(bip.nPeriod+1 - bip.threshold, bip.nStartTime, 0);
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))!=ThresholdState::LOCKED_IN);
    }
}

BOOST_AUTO_TEST_CASE(willTransitionFromDefinedStateToStartedStateOnlyIfMedianBlockTimeIsAtLeastStartTime)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod+1, bip.nStartTime - 1, 1 );
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))!=ThresholdState::STARTED);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime - 1, 1 );
        fakeChain.extendBy(1 + CBlockIndex::nMedianTimeSpan, bip.nStartTime, 1 );
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::STARTED);
    }
}

BOOST_AUTO_TEST_CASE(willTransitionFromDefinedOrStartedStateToFailedStateIfMedianBlockTimeIsAtLeastTimeout)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime - 1, 1 );
        fakeChain.extendBy(1 + CBlockIndex::nMedianTimeSpan, bip.nTimeout, 1 );
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::FAILED);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime - 1, 1 );
        fakeChain.extendBy(1 + CBlockIndex::nMedianTimeSpan, bip.nTimeout, 1 );
        cache[fakeChain.at(0)] = ThresholdState::STARTED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::FAILED);
    }
}

BOOST_AUTO_TEST_CASE(willOverrideTransitionToOtherStateIfMedianBlockTimeIsAtLeastTimeout)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        int32_t bipMask = ( (int32_t)1 << bip.bit);

        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime +1, VERSIONBITS_TOP_BITS | bipMask );
        fakeChain.extendBy(1 + CBlockIndex::nMedianTimeSpan, bip.nTimeout, 1 );
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::FAILED);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        int32_t bipMask = ( (int32_t)1 << bip.bit);

        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod - CBlockIndex::nMedianTimeSpan, bip.nStartTime +1, VERSIONBITS_TOP_BITS | bipMask );
        fakeChain.extendBy(1 + CBlockIndex::nMedianTimeSpan, bip.nTimeout, 1 );
        cache[fakeChain.at(0)] = ThresholdState::STARTED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::FAILED);
    }
}

static void testTransitionOccursRegardless(ThresholdState start, ThresholdState end)
{
    {
        BIP9Deployment bip = createViableBipDeployment();
        int32_t bipMask = ( (int32_t)1 << bip.bit);

        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod, bip.nStartTime +1, VERSIONBITS_TOP_BITS | bipMask );
        fakeChain.extendBy(1, bip.nTimeout, 1 );
        cache[fakeChain.at(0)] = start;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==end);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();
        int32_t bipMask = ( (int32_t)1 << bip.bit);

        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod+1, bip.nTimeout +1, VERSIONBITS_TOP_BITS | bipMask );
        cache[fakeChain.at(0)] = start;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==end);
    }
    {
        BIP9Deployment bip = createViableBipDeployment();

        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod+1, bip.nTimeout +1, 1 );
        cache[fakeChain.at(0)] = start;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==end);
    }

}
BOOST_AUTO_TEST_CASE(willTransitionFromLockedInToActiveRegardlessOfBlockState)
{
    testTransitionOccursRegardless(ThresholdState::LOCKED_IN,ThresholdState::ACTIVE);
}

BOOST_AUTO_TEST_CASE(willTransitionToItsPresentStateFromFixedStates)
{
    testTransitionOccursRegardless(ThresholdState::ACTIVE,ThresholdState::ACTIVE);
    testTransitionOccursRegardless(ThresholdState::FAILED,ThresholdState::FAILED);
}


BOOST_AUTO_TEST_CASE(willReachFailedStateOnUpdateWithAnInvalidBip)
{
    {
        BIP9Deployment bip = createTimedOutBipDeployment();
        ThresholdConditionCache cache;
        FakeBlockIndexChain fakeChain;
        fakeChain.extendBy(bip.nPeriod+1, bip.nTimeout - 1, 1 );
        cache[fakeChain.at(0)] = ThresholdState::DEFINED;

        CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

        BOOST_CHECK(activationStateTracker.update(fakeChain.at(bip.nPeriod)));
        BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(bip.nPeriod))==ThresholdState::FAILED);
        BOOST_CHECK( cache.count(fakeChain.at(bip.nPeriod)) &&
            cache[fakeChain.at(bip.nPeriod)] ==ThresholdState::FAILED  );
    }
}

BOOST_AUTO_TEST_CASE(willUpdateOnlyCacheEntriesAfterStartTime)
{
    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    fakeChain.extendBy(2*bip.nPeriod, bip.nStartTime - 1, 1 );
    fakeChain.extendBy(bip.nPeriod+1, bip.nStartTime + 1, 1 );
    cache[fakeChain.at(0)] = ThresholdState::DEFINED;

    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    BOOST_CHECK(activationStateTracker.update(fakeChain.at(3*bip.nPeriod)));
    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(3*bip.nPeriod))==ThresholdState::STARTED);
    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(2*bip.nPeriod))==ThresholdState::DEFINED);
    BOOST_CHECK( cache.count(fakeChain.at(bip.nPeriod))==0 );
    BOOST_CHECK( cache.count(fakeChain.at(2*bip.nPeriod)) );
    BOOST_CHECK( cache.count(fakeChain.at(3*bip.nPeriod)) );
}

BOOST_AUTO_TEST_CASE(willUpdateAllCacheEntriesAfterStartTime)
{
    BIP9Deployment bip = createViableBipDeployment();
    ThresholdConditionCache cache;
    FakeBlockIndexChain fakeChain;
    int32_t version = VERSIONBITS_TOP_BITS | ( (int32_t)1 << bip.bit);

    fakeChain.extendBy(2*bip.nPeriod, bip.nStartTime - 1, 1 );
    fakeChain.extendBy(bip.nPeriod, bip.nStartTime + 1, 1 );
    fakeChain.extendBy(bip.nPeriod, bip.nStartTime + 1, version );
    fakeChain.extendBy(bip.nPeriod, bip.nStartTime + 1, 1 );

    CachedBIP9ActivationStateTracker activationStateTracker(bip,cache);

    BOOST_CHECK(activationStateTracker.update(fakeChain.at(4*bip.nPeriod)));
    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(4*bip.nPeriod))==ThresholdState::LOCKED_IN);
    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(3*bip.nPeriod))==ThresholdState::STARTED);
    BOOST_CHECK(activationStateTracker.getLastCachedStatePriorToBlockIndex(fakeChain.at(2*bip.nPeriod))==ThresholdState::DEFINED);
    BOOST_CHECK( cache.count(fakeChain.at(bip.nPeriod))==0 );
    BOOST_CHECK( cache.count(fakeChain.at(2*bip.nPeriod)) );
    BOOST_CHECK( cache.count(fakeChain.at(3*bip.nPeriod)) );
    BOOST_CHECK( cache.count(fakeChain.at(4*bip.nPeriod)) );
}

BOOST_AUTO_TEST_SUITE_END();


#include <random.h>
#include <memory>
#include <utility>

namespace TestHelpers
{
static int32_t TestTime(int nHeight) { return 1415926536 + 600 * nHeight; }

struct RandomnessProvider
{
private:
    FastRandomContext context_;
public:
    RandomnessProvider(): context_(true) {}
    uint32_t InsecureRandBits(int bits)
    {
        assert(bits>-1);
        return context_.rand32() & ( (uint32_t)1 << bits);
    }
};

static BIP9Deployment createDummyBIP(std::string name = std::string("DummyDeployment"))
{
    return BIP9Deployment(name, 0 , TestTime(10000),TestTime(20000),1000,900);
}

struct TestCachedBIP9ActivationStateTracker: public CachedBIP9ActivationStateTracker
{
    TestCachedBIP9ActivationStateTracker(
        const BIP9Deployment& bip,
        ThresholdConditionCache& thresholdCache
        ): CachedBIP9ActivationStateTracker(bip,thresholdCache)
    {
    }

    virtual bool bipIsSignaledFor(const CBlockIndex* pindex) const
    {
        return (pindex->nVersion & 0x100);
    }
};
}

class TestWrapper
{
private:
    int n;
    BIP9Deployment dummyDeployment;
    ThresholdConditionCache cache;
public:
    std::shared_ptr<I_BIP9ActivationStateTracker> tracker;
    std::vector<CBlockIndex*> fakeChain;
    int testCounter;

    void clearFakeChain()
    {
        for(auto ptr: fakeChain)
        {
            delete ptr;
        }
        fakeChain.clear();
    }

    explicit TestWrapper(
        int randomnessBit = -1,
        bool makeAlwaysActive = false
        ): n(randomnessBit)
        , dummyDeployment(TestHelpers::createDummyBIP())
        , cache()
        , tracker(std::make_shared<TestHelpers::TestCachedBIP9ActivationStateTracker>(dummyDeployment,cache))
        , fakeChain()
        , testCounter(0)
    {
        if(makeAlwaysActive)
        {
            *const_cast<int64_t*>(&dummyDeployment.nStartTime) = BIP9Deployment::ALWAYS_ACTIVE;
        }
    }

    TestWrapper& operator=(const TestWrapper& other)
    {
        dummyDeployment = other.dummyDeployment;
        cache = other.cache;
        tracker.reset(new TestHelpers::TestCachedBIP9ActivationStateTracker(dummyDeployment,cache));
        fakeChain = other.fakeChain;
        testCounter =other.testCounter;
        return *this;
    }

    ~TestWrapper()
    {
        clearFakeChain();
    }

    TestWrapper& Reset()
    {
        clearFakeChain();
        cache.clear();
        tracker.reset(new TestHelpers::TestCachedBIP9ActivationStateTracker(dummyDeployment,cache));
        return *this;
    }

    TestWrapper& Mine(unsigned newHeight,int32_t blockTime, int32_t blockVersion)
    {
        FakeBlockIndexChain::extendFakeBlockIndexChain(newHeight+1,blockTime,blockVersion,fakeChain);
        return *this;
    }
    TestWrapper& TestState(ThresholdState state)
    {
        static TestHelpers::RandomnessProvider rand;
        if( n > -1 && rand.InsecureRandBits(n % 32)) return *this;

        const CBlockIndex* blockToUpdate = fakeChain.size()? fakeChain.back(): NULL;

        BOOST_CHECK_MESSAGE( tracker->update(blockToUpdate), "Update failed!");

        ThresholdState result = tracker->getLastCachedStatePriorToBlockIndex(blockToUpdate);
        BOOST_CHECK_MESSAGE(
            result == state,
            "Comparing: (statecode)" << static_cast<int>(result)
            << " vs " << static_cast<int>(state) << " || testCounter: " << testCounter);
        testCounter++;
        return *this;
    }

    const ThresholdConditionCache& getCache() const
    {
        return cache;
    }

    TestWrapper& TestDefined()
    {
        return TestState(ThresholdState::DEFINED);
    }
    TestWrapper& TestStarted()
    {
        return TestState(ThresholdState::STARTED);
    }
    TestWrapper& TestLockedIn()
    {
        return TestState(ThresholdState::LOCKED_IN);
    }
    TestWrapper& TestFailed()
    {
        return TestState(ThresholdState::FAILED);
    }
    TestWrapper& TestActive()
    {
        return TestState(ThresholdState::ACTIVE);
    }

    TestWrapper& TestStateSinceHeight(int height)
    {
        return *this;
    }
};


BOOST_AUTO_TEST_SUITE(CacheTestsForBIP9ActivationTracker)


BOOST_AUTO_TEST_CASE(willTransitionFromDefinedToFailedDueToTimeout)
{
    using namespace TestHelpers;

    TestWrapper test;
    test.TestDefined();
    test.Mine(1, TestTime(1), 0x100).TestDefined();
    test.Mine(11, TestTime(11), 0x100).TestDefined();
    test.Mine(989, TestTime(989), 0x100).TestDefined();
    test.Mine(999, TestTime(20000), 0x100).TestDefined();
    test.Mine(1000, TestTime(20000), 0x100).TestFailed();
    test.Mine(1999, TestTime(30001), 0x100).TestFailed();
    test.Mine(2000, TestTime(30002), 0x100).TestFailed();
    test.Mine(2001, TestTime(30003), 0x100).TestFailed();
    test.Mine(2999, TestTime(30004), 0x100).TestFailed();
    test.Mine(3000, TestTime(30005), 0x100).TestFailed();
}

BOOST_AUTO_TEST_CASE(willTransitionFromDefinedToStartedToFailedDueToTimeout)
{
    using namespace TestHelpers;

    TestWrapper test;
    test.TestDefined();
    test.Mine(1, TestTime(1), 0).TestDefined();
    test.Mine(1000, TestTime(10000) - 1, 0x100).TestDefined();// One second more and it would be defined
    test.Mine(2000, TestTime(10000), 0x100).TestStarted();   // So that's what happens the next period
    test.Mine(2051, TestTime(10010), 0).TestStarted();   // 51 old blocks
    test.Mine(2950, TestTime(10020), 0x100).TestStarted();   // 899 new blocks
    test.Mine(3000, TestTime(20000), 0).TestFailed();   // 50 old blocks (so 899 out of the past 1000)
    test.Mine(4000, TestTime(20010), 0x100).TestFailed();
}

BOOST_AUTO_TEST_CASE(willFailToTransitionIntoALockedInStateDueToTimeoutEvenIfBipIsSignaled)
{
    using namespace TestHelpers;
    TestWrapper test;

    test.TestDefined();
    test.Mine(1, TestTime(1), 0).TestDefined();
    test.Mine(1000, TestTime(10000) - 1, 0x101).TestDefined();// One second more and it would be defined
    test.Mine(2000, TestTime(10000), 0x101).TestStarted();   // So that's what happens the next period
    test.Mine(2999, TestTime(30000), 0x100).TestStarted();   // 999 new blocks
    test.Mine(3000, TestTime(30000), 0x100).TestFailed();   // 1 new block (so 1000 out of the past 1000 are new)
    test.Mine(3999, TestTime(30001), 0).TestFailed();
    test.Mine(4000, TestTime(30002), 0).TestFailed();
    test.Mine(14333, TestTime(30003), 0).TestFailed();
    test.Mine(24000, TestTime(40000), 0).TestFailed();
}

BOOST_AUTO_TEST_CASE(willTransitionFromDefinedToActiveNormally)
{
    using namespace TestHelpers;

    TestWrapper test;
    test.TestDefined().Mine(1, TestTime(1), 0).TestDefined();
    test.Mine(1000, TestTime(10000) - 1, 0x101).TestDefined();
    test.Mine(2000, TestTime(10000), 0x101).TestStarted();
    test.Mine(2050, TestTime(10010), 0x200).TestStarted();
    test.Mine(2950, TestTime(10020), 0x100).TestStarted();
    test.Mine(2999, TestTime(19999), 0x200).TestStarted();
    test.Mine(3999, TestTime(30001), 0).TestLockedIn();
    test.Mine(4000, TestTime(30002), 0).TestActive();
    test.Mine(14333, TestTime(30003), 0).TestActive();
    test.Mine(24000, TestTime(40000), 0).TestActive();

}

BOOST_AUTO_TEST_CASE(willRemainInStartedStateUntilTimeoutAndRemainInFailedStateAfterwards)
{
    using namespace TestHelpers;

    TestWrapper test;
    test.TestDefined();
    test.Mine(999, TestTime(999), 0).TestDefined();
    test.Mine(1000, TestTime(1000), 0).TestDefined();
    test.Mine(2000, TestTime(2000), 0).TestDefined();
    test.Mine(3000, TestTime(10000), 0).TestStarted();
    test.Mine(4000, TestTime(10000), 0).TestStarted();
    test.Mine(5000, TestTime(10000), 0).TestStarted();
    test.Mine(6000, TestTime(20000), 0).TestFailed();
    test.Mine(7000, TestTime(20000), 0x100).TestFailed();
}

BOOST_AUTO_TEST_CASE(willTransitionCorrectlyIndependentlyOfMethodInvocationOrder)
{
    using namespace TestHelpers;
    for (int i = 0; i < 64; i++) {
        // DEFINED -> FAILED
        TestWrapper(i).TestDefined().TestStateSinceHeight(0)
                           .Mine(1, TestTime(1), 0x100).TestDefined().TestStateSinceHeight(0)
                           .Mine(11, TestTime(11), 0x100).TestDefined().TestStateSinceHeight(0)
                           .Mine(989, TestTime(989), 0x100).TestDefined().TestStateSinceHeight(0)
                           .Mine(999, TestTime(20000), 0x100).TestDefined().TestStateSinceHeight(0)
                           .Mine(1000, TestTime(20000), 0x100).TestFailed().TestStateSinceHeight(1000)
                           .Mine(1999, TestTime(30001), 0x100).TestFailed().TestStateSinceHeight(1000)
                           .Mine(2000, TestTime(30002), 0x100).TestFailed().TestStateSinceHeight(1000)
                           .Mine(2001, TestTime(30003), 0x100).TestFailed().TestStateSinceHeight(1000)
                           .Mine(2999, TestTime(30004), 0x100).TestFailed().TestStateSinceHeight(1000)
                           .Mine(3000, TestTime(30005), 0x100).TestFailed().TestStateSinceHeight(1000)

        // DEFINED -> STARTED -> FAILED
                           .Reset().TestDefined().TestStateSinceHeight(0)
                           .Mine(1, TestTime(1), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(1000, TestTime(10000) - 1, 0x100).TestDefined().TestStateSinceHeight(0) // One second more and it would be defined
                           .Mine(2000, TestTime(10000), 0x100).TestStarted().TestStateSinceHeight(2000) // So that's what happens the next period
                           .Mine(2051, TestTime(10010), 0).TestStarted().TestStateSinceHeight(2000) // 51 old blocks
                           .Mine(2950, TestTime(10020), 0x100).TestStarted().TestStateSinceHeight(2000) // 899 new blocks
                           .Mine(3000, TestTime(20000), 0).TestFailed().TestStateSinceHeight(3000) // 50 old blocks (so 899 out of the past 1000)
                           .Mine(4000, TestTime(20010), 0x100).TestFailed().TestStateSinceHeight(3000)

        // DEFINED -> STARTED -> FAILED while threshold reached
                           .Reset().TestDefined().TestStateSinceHeight(0)
                           .Mine(1, TestTime(1), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(1000, TestTime(10000) - 1, 0x101).TestDefined().TestStateSinceHeight(0) // One second more and it would be defined
                           .Mine(2000, TestTime(10000), 0x101).TestStarted().TestStateSinceHeight(2000) // So that's what happens the next period
                           .Mine(2999, TestTime(30000), 0x100).TestStarted().TestStateSinceHeight(2000) // 999 new blocks
                           .Mine(3000, TestTime(30000), 0x100).TestFailed().TestStateSinceHeight(3000) // 1 new block (so 1000 out of the past 1000 are new)
                           .Mine(3999, TestTime(30001), 0).TestFailed().TestStateSinceHeight(3000)
                           .Mine(4000, TestTime(30002), 0).TestFailed().TestStateSinceHeight(3000)
                           .Mine(14333, TestTime(30003), 0).TestFailed().TestStateSinceHeight(3000)
                           .Mine(24000, TestTime(40000), 0).TestFailed().TestStateSinceHeight(3000)

        // DEFINED -> STARTED -> LOCKEDIN at the last minute -> ACTIVE
                           .Reset().TestDefined()
                           .Mine(1, TestTime(1), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(1000, TestTime(10000) - 1, 0x101).TestDefined().TestStateSinceHeight(0) // One second more and it would be defined
                           .Mine(2000, TestTime(10000), 0x101).TestStarted().TestStateSinceHeight(2000) // So that's what happens the next period
                           .Mine(2050, TestTime(10010), 0x200).TestStarted().TestStateSinceHeight(2000) // 50 old blocks
                           .Mine(2950, TestTime(10020), 0x100).TestStarted().TestStateSinceHeight(2000) // 900 new blocks
                           .Mine(2999, TestTime(19999), 0x200).TestStarted().TestStateSinceHeight(2000) // 49 old blocks
                           .Mine(3000, TestTime(29999), 0x200).TestLockedIn().TestStateSinceHeight(3000) // 1 old block (so 900 out of the past 1000)
                           .Mine(3999, TestTime(30001), 0).TestLockedIn().TestStateSinceHeight(3000)
                           .Mine(4000, TestTime(30002), 0).TestActive().TestStateSinceHeight(4000)
                           .Mine(14333, TestTime(30003), 0).TestActive().TestStateSinceHeight(4000)
                           .Mine(24000, TestTime(40000), 0).TestActive().TestStateSinceHeight(4000)

        // DEFINED multiple periods -> STARTED multiple periods -> FAILED
                           .Reset().TestDefined().TestStateSinceHeight(0)
                           .Mine(999, TestTime(999), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(1000, TestTime(1000), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(2000, TestTime(2000), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(3000, TestTime(10000), 0).TestStarted().TestStateSinceHeight(3000)
                           .Mine(4000, TestTime(10000), 0).TestStarted().TestStateSinceHeight(3000)
                           .Mine(5000, TestTime(10000), 0).TestStarted().TestStateSinceHeight(3000)
                           .Mine(6000, TestTime(20000), 0).TestFailed().TestStateSinceHeight(6000)
                           .Mine(7000, TestTime(20000), 0x100).TestFailed().TestStateSinceHeight(6000);
    }

}

BOOST_AUTO_TEST_SUITE_END();