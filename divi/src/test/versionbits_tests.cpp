// Copyright (c) 2014-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
//#include <test/util/setup_common.h>
//#include <validation.h>
#include <versionbits.h>
#include <random.h>

#include <boost/test/unit_test.hpp>
#include <test_only.h>

/* Define a virtual block time, one block per 10 minutes after Nov 14 2014, 0:55:36am */
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
        return context_.rand32() % ( 1 << bits);
    }
};

static BIP9Deployment createDummyBIP(std::string name = std::string("DummyDeployment"))
{
    return BIP9Deployment(name, 0 , TestTime(10000),TestTime(20000),1000,900);
}

class BlockVersionProvider
{
private:
    VersionBitsCache versionBitsCache_;
public:
    BlockVersionProvider(): versionBitsCache_() {}

    int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const BIP9Deployment& bip)
    {
        int32_t nVersion = VERSIONBITS_TOP_BITS;

        for (int i = 0; i < (int)BIP9Deployment::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
            ThresholdState state = VersionBitsState(pindexPrev, bip, versionBitsCache_);
            if (state == ThresholdState::LOCKED_IN || state == ThresholdState::STARTED) {
                nVersion |= VersionBitsMask(bip);
            }
            bip.setState(state);
        }
        
        return nVersion;
    }
};


class TestConditionChecker : public AbstractThresholdConditionChecker
{
private:
    mutable ThresholdConditionCache cache;
protected:
    BIP9Deployment dummyDeployment;
public:
    TestConditionChecker(
        ): AbstractThresholdConditionChecker(dummyDeployment)
        , dummyDeployment("DummyDeployment",0,TestTime(10000),TestTime(20000),1000,900)
    {
    }

    bool Condition(const CBlockIndex* pindex) const override { return (pindex->nVersion & 0x100); }

    ThresholdState GetStateFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::UpdateCacheState(pindexPrev, cache); }
    int StartingHeightOfBlockIndexState(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::StartingHeightOfBlockIndexState(pindexPrev, cache); }
};

class TestAlwaysActiveConditionChecker : public TestConditionChecker
{
public:
    TestAlwaysActiveConditionChecker()
    {
        *const_cast<int64_t*>(&dummyDeployment.nStartTime) = BIP9Deployment::ALWAYS_ACTIVE;
        dummyDeployment.state = ThresholdState::ACTIVE;
    }
};

#define CHECKERS 6

class VersionBitsTester
{
    // Randomness Source
    RandomnessProvider rand;
    uint32_t InsecureRandBits(int bits){ return rand.InsecureRandBits(bits);}

    // A fake blockchain
    std::vector<CBlockIndex*> vpblock;

    // 6 independent checkers for the same bit.
    // The first one performs all checks, the second only 50%, the third only 25%, etc...
    // This is to test whether lack of cached information leads to the same results.
    TestConditionChecker checker[CHECKERS];
    // Another 6 that assume always active activation
    TestAlwaysActiveConditionChecker checker_always[CHECKERS];

    // Test counter (to identify failures)
    int num;

public:
    VersionBitsTester() : rand(), num(0) {}

    VersionBitsTester& Reset() {
        for (unsigned int i = 0; i < vpblock.size(); i++) {
            delete vpblock[i];
        }
        for (unsigned int  i = 0; i < CHECKERS; i++) {
            checker[i] = TestConditionChecker();
            checker_always[i] = TestAlwaysActiveConditionChecker();
        }
        vpblock.clear();
        return *this;
    }

    ~VersionBitsTester() {
         Reset();
    }

    VersionBitsTester& Mine(unsigned int height, int32_t nTime, int32_t nVersion) {
        while (vpblock.size() < height) {
            CBlockIndex* pindex = new CBlockIndex();
            pindex->nHeight = vpblock.size();
            pindex->pprev = vpblock.size() > 0 ? vpblock.back() : nullptr;
            pindex->nTime = nTime;
            pindex->nVersion = nVersion;
            pindex->BuildSkip();
            vpblock.push_back(pindex);
        }
        return *this;
    }

    VersionBitsTester& TestStateSinceHeight(int height) {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].StartingHeightOfBlockIndexState(vpblock.empty() ? nullptr : vpblock.back()) == height, strprintf("Test %i for StateSinceHeight", num));
                BOOST_CHECK_MESSAGE(checker_always[i].StartingHeightOfBlockIndexState(vpblock.empty() ? nullptr : vpblock.back()) == 0, strprintf("Test %i for StateSinceHeight (always active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestDefined() {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::DEFINED, strprintf("Test %i for DEFINED", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE (always active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestStarted() {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::STARTED, strprintf("Test %i for STARTED", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE (always active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestLockedIn() {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::LOCKED_IN, strprintf("Test %i for LOCKED_IN", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE (always active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestActive() {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE (always active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestFailed() {
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::FAILED, strprintf("Test %i for FAILED", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateFor(vpblock.empty() ? nullptr : vpblock.back()) == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE (always active)", num));
            }
        }
        num++;
        return *this;
    }

    CBlockIndex * Tip() { return vpblock.size() ? vpblock.back() : nullptr; }
};

BOOST_AUTO_TEST_SUITE(versionbits_tests)

BOOST_AUTO_TEST_CASE(versionbits_test)
{
    for (int i = 0; i < 64; i++) {
        // DEFINED -> FAILED
        VersionBitsTester().TestDefined().TestStateSinceHeight(0)
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

BOOST_AUTO_TEST_CASE(versionbits_computeblockversion)
{
    // Use the TESTDUMMY deployment for testing purposes.
    BIP9Deployment dummyDeploy = createDummyBIP();
    BlockVersionProvider versionProvider;

    assert(dummyDeploy.nStartTime < dummyDeploy.nTimeout);

    // In the first chain, test that the bit is set by CBV until it has failed.
    // In the second chain, test the bit is set by CBV while STARTED and
    // LOCKED-IN, and then no longer set while ACTIVE.
    VersionBitsTester firstChain, secondChain;

    // Start generating blocks before nStartTime
    int64_t nTime = dummyDeploy.nStartTime - 1;

    // Before MedianTimePast of the chain has crossed nStartTime, the bit
    // should not be set.
    CBlockIndex *lastBlock = nullptr;
    lastBlock = firstChain.Mine(dummyDeploy.nPeriod, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit), 0);

    // Mine more blocks (4 less than the adjustment period) at the old time, and check that CBV isn't setting the bit yet.
    for (uint32_t i = 1; i <(uint32_t)  dummyDeploy.nPeriod - 4; i++) {
        lastBlock = firstChain.Mine(dummyDeploy.nPeriod + i, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        // This works because VERSIONBITS_LAST_OLD_BLOCK_VERSION happens
        // to be 4, and the bit we're testing happens to be bit 28.
        BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1 << dummyDeploy.bit), 0);
    }
    // Now mine 5 more blocks at the start time -- MTP should not have passed yet, so
    // CBV should still not yet set the bit.
    nTime = dummyDeploy.nStartTime;
    for (uint32_t i =(uint32_t)  dummyDeploy.nPeriod - 4; i <=(uint32_t)  dummyDeploy.nPeriod; i++) {
        lastBlock = firstChain.Mine(dummyDeploy.nPeriod + i, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit), 0);
    }

    // Advance to the next period and transition to STARTED,
    lastBlock = firstChain.Mine(dummyDeploy.nPeriod * 3, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    // so ComputeBlockVersion should now set the bit,
    BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit)) != 0);
    // and should also be using the VERSIONBITS_TOP_BITS.
    BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);

    // Check that ComputeBlockVersion will set the bit until nTimeout
    nTime += 600;
    uint32_t blocksToMine = dummyDeploy.nPeriod * 2; // test blocks for up to 2 time periods
    uint32_t nHeight = dummyDeploy.nPeriod * 3;
    // These blocks are all before nTimeout is reached.
    while (nTime < dummyDeploy.nTimeout && blocksToMine > 0) {
        lastBlock = firstChain.Mine(nHeight+1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit)) != 0);
        BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);
        blocksToMine--;
        nTime += 600;
        nHeight += 1;
    }

    nTime = dummyDeploy.nTimeout;
    // FAILED is only triggered at the end of a period, so CBV should be setting
    // the bit until the period transition.
    for (uint32_t i = 0; i <(uint32_t)  dummyDeploy.nPeriod - 1; i++) {
        lastBlock = firstChain.Mine(nHeight+1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit)) != 0);
        nHeight += 1;
    }
    // The next block should trigger no longer setting the bit.
    lastBlock = firstChain.Mine(nHeight+1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit), 0);

    // On a new chain:
    // verify that the bit will be set after lock-in, and then stop being set
    // after activation.
    nTime = dummyDeploy.nStartTime;

    // Mine one period worth of blocks, and check that the bit will be on for the
    // next period.
    lastBlock = secondChain.Mine(dummyDeploy.nPeriod, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit)) != 0);

    // Mine another period worth of blocks, signaling the new bit.
    lastBlock = secondChain.Mine(dummyDeploy.nPeriod * 2, nTime, VERSIONBITS_TOP_BITS | (1<<dummyDeploy.bit)).Tip();
    // After one period of setting the bit on each block, it should have locked in.
    // We keep setting the bit for one more period though, until activation.
    BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit)) != 0);

    // Now check that we keep mining the block until the end of this period, and
    // then stop at the beginning of the next period.
    lastBlock = secondChain.Mine((dummyDeploy.nPeriod * 3) - 1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK((versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1 << dummyDeploy.bit)) != 0);
    lastBlock = secondChain.Mine(dummyDeploy.nPeriod * 3, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK_EQUAL(versionProvider.ComputeBlockVersion(lastBlock,dummyDeploy) & (1<<dummyDeploy.bit), 0);

    // Finally, verify that after a soft fork has activated, CBV no longer uses
    // VERSIONBITS_LAST_OLD_BLOCK_VERSION.
    //BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, mainnetParams) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);
}

BOOST_AUTO_TEST_SUITE_END()
