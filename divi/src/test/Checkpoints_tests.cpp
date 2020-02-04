// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

#include "blockmap.h"
#include "uint256.h"
#include "random.h"

#include <boost/test/unit_test.hpp>
#include "test_only.h"
using namespace std;

class TestCase
{
private:
    int maxNumberOfBlocks = 800000;
public:
    TestCase()
    {
        checkpointCount_ = 0;
        data_ = CCheckpointData({&mapCheckpoints_,0,0,0});
    }

    TestCase(unsigned int numberOfCheckpoints): checkpointCount_(numberOfCheckpoints)
    {
        for(unsigned j = 0; j < numberOfCheckpoints; j++)
        {
            blockIndices_.push_back(GetRandInt(maxNumberOfBlocks));
            blockHashes_.push_back(GetRandHash());
            mapCheckpoints_.insert({blockIndices_.back(),blockHashes_.back()});
        }
        data_ = CCheckpointData({&mapCheckpoints_,0,0,0});
    }

    void addCheckpoint(
        int blockIndex, 
        uint256 hash, 
        int64_t checkpointTime = 0, 
        int64_t transactionsAtCheckpoint = 0)
    {
        const double transactionsPerDay = 86400.0;
        blockIndices_.push_back(blockIndex);
        blockHashes_.push_back(hash);
        mapCheckpoints_.insert({blockIndex,hash});

        data_.nTimeLastCheckpoint = checkpointTime;
        data_.nTransactionsLastCheckpoint = transactionsAtCheckpoint;
        data_.fTransactionsPerDay = 
            (data_.nTransactionsLastCheckpoint>0)? data_.nTransactionsLastCheckpoint/static_cast<double>(data_.nTimeLastCheckpoint/transactionsPerDay): int64_t(0);
    }

    const CCheckpointData& checkpoint_data() const
    {
        return data_;
    }
    const std::pair<int,uint256> getRandomCorrectCheckpoint()
    {
        return *std::next(mapCheckpoints_.begin(), (abs(GetRandInt(maxNumberOfBlocks)) % checkpointCount_));
    }
    const std::pair<std::pair<int,uint256>,bool> getRandomCheckpointAndExpectation()
    {
        std::pair<int,uint256> randomPair({GetRandInt(maxNumberOfBlocks),GetRandHash()});
        auto it = mapCheckpoints_.find(randomPair.first);
        if(it != mapCheckpoints_.end())
        {
            return std::make_pair(randomPair, randomPair.second == it->second);
        }
        return std::make_pair(randomPair, true);
    }

    unsigned int checkpointCount_;
    std::vector<int> blockIndices_;
    std::vector<uint256> blockHashes_;
    MapCheckpoints mapCheckpoints_;
    CCheckpointData data_;
};

BOOST_AUTO_TEST_SUITE(Checkpoints_tests)

BOOST_AUTO_TEST_CASE(allCheckpointsWillBeAccountedFor)
{   // Exhaustive search for correctness
    unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
    TestCase testSetup(checkpointCount);
    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );

    for(const auto& checkpoint: testSetup.mapCheckpoints_)
    {
        BOOST_CHECK(checkpointsService.CheckBlock(checkpoint.first,checkpoint.second));
    }
}

BOOST_AUTO_TEST_CASE(randomCheckpointsWillBeCorrectlyHandled)
{   
    unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
    TestCase testSetup(checkpointCount);
    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );

    for(unsigned checkpointIndex = 0; checkpointIndex < checkpointCount; checkpointIndex++)
    {
        std::pair<std::pair<int, uint256>,bool> checkpointAndExpectation = testSetup.getRandomCheckpointAndExpectation();
        std::pair<int, uint256> checkpoint = checkpointAndExpectation.first;
        BOOST_CHECK(checkpointsService.CheckBlock(checkpoint.first,checkpoint.second) == checkpointAndExpectation.second);
    }
}


BOOST_AUTO_TEST_CASE(deliberatlyIncorrectCheckpointsWillBeCorrectlyHandled)
{
    unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
    TestCase testSetup(checkpointCount);
    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );

    for(unsigned checkpointIndex = 0; checkpointIndex < checkpointCount; checkpointIndex++)
    {
        std::pair<int, uint256> checkpoint = testSetup.getRandomCorrectCheckpoint();
        uint256 correctHash = checkpoint.second;
        while(checkpoint.second == correctHash)
        {
            checkpoint.second = GetRandHash();
        }
        BOOST_CHECK(!checkpointsService.CheckBlock(checkpoint.first,checkpoint.second));
    }
}

BOOST_AUTO_TEST_CASE(willNotFailDueToLackOfCheckpoints)
{
    TestCase testSetup(0);
    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );
    BlockMap map;

    BOOST_CHECK(checkpointsService.CheckBlock(GetRandInt(25),GetRandHash()));
    BOOST_CHECK(checkpointsService.GetTotalBlocksEstimate()==0);
    BOOST_CHECK(checkpointsService.GuessVerificationProgress(nullptr)==0.0);
    BOOST_CHECK(checkpointsService.GetLastCheckpoint(map)==nullptr);
}


BOOST_AUTO_TEST_CASE(willFindLargestHeightAmongstCheckpoints)
{
    unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
    TestCase testSetup(checkpointCount);
    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );

    int highestBlockInCheckpoints = 0;
    for(const auto& checkpoint: testSetup.mapCheckpoints_)
    {
        highestBlockInCheckpoints = std::max(highestBlockInCheckpoints, checkpoint.first);
    }

    BOOST_CHECK(checkpointsService.GetTotalBlocksEstimate()==highestBlockInCheckpoints);
}


BOOST_AUTO_TEST_CASE(willFindCorrectBlockInMap)
{
    std::shared_ptr<CBlockIndex> sharedBlockIndex = std::make_shared<CBlockIndex>();
    uint256 blockHash = GetRandHash();
    sharedBlockIndex->phashBlock = &blockHash;
    sharedBlockIndex->nHeight = GetRandInt(800);
    TestCase testSetup;
    testSetup.mapCheckpoints_.insert( 
        std::make_pair(static_cast<int>(sharedBlockIndex->nHeight),sharedBlockIndex->GetBlockHash()) );
    testSetup.data_ = CCheckpointData({&testSetup.mapCheckpoints_,0,0,0});
    
    BlockMap map;
    map.insert( std::make_pair(sharedBlockIndex->GetBlockHash(),sharedBlockIndex.get()) );

    CCheckpointServices checkpointsService( testSetup.checkpoint_data() );

    BOOST_CHECK(checkpointsService.GetLastCheckpoint(map)!=nullptr);
    BOOST_CHECK(checkpointsService.GetLastCheckpoint(map)->nHeight == sharedBlockIndex->nHeight);
}

BOOST_AUTO_TEST_CASE(willEstimateProgressCorrectly)
{
    std::shared_ptr<CBlockIndex> sharedBlockIndex = std::make_shared<CBlockIndex>();
    sharedBlockIndex->nChainTx = 10000;
    sharedBlockIndex->nTime = 1580841426u;

    {// _As100PercentWithoutCheckpoints
        TestCase testSetup;
        CCheckpointServices checkpointsService(testSetup.checkpoint_data());
        double progress = checkpointsService.GuessVerificationProgress(sharedBlockIndex.get(),false);
        BOOST_CHECK_CLOSE(progress,1.0, 0.0001);
    }
    {// _CorrectlyWithcheckpointPrior
        TestCase testSetup;
        double transactionsAtCheckpoint = static_cast<double>(sharedBlockIndex->nChainTx -1 -abs(GetRandInt(1000)));
        int64_t checkpointTimeAtStart = sharedBlockIndex->nTime - 1000000u;

        testSetup.addCheckpoint(GetRandInt(100),GetRandHash(), checkpointTimeAtStart, transactionsAtCheckpoint);

        double transactionsPerDay = testSetup.data_.fTransactionsPerDay;
        double secondsPerDay = 86400.0;
        double numberOfDays = (static_cast<int64_t>(time(NULL)) - sharedBlockIndex->GetBlockTime())/secondsPerDay;
        CCheckpointServices checkpointsService(testSetup.checkpoint_data());
        double initialTxs = static_cast<double>(sharedBlockIndex->nChainTx);
        double estimateOfTotalTransactions = initialTxs + static_cast<double>(transactionsPerDay*numberOfDays);

        double progress = checkpointsService.GuessVerificationProgress(sharedBlockIndex.get(),false);

        BOOST_CHECK_CLOSE(progress, initialTxs/estimateOfTotalTransactions, 0.01);
    }
    {// _CorrectlyWithcheckpointAfter
        TestCase testSetup;
        double transactionsAtCheckpoint = static_cast<double>(sharedBlockIndex->nChainTx + 1 + abs(GetRandInt(1000)));
        int64_t checkpointTimeAtStart = sharedBlockIndex->nTime + 1000000u;

        testSetup.addCheckpoint(GetRandInt(100),GetRandHash(), checkpointTimeAtStart, transactionsAtCheckpoint);

        double transactionsPerDay = testSetup.data_.fTransactionsPerDay;
        double secondsPerDay = 86400.0;
        double numberOfDays = (static_cast<int64_t>(time(NULL)) - checkpointTimeAtStart)/secondsPerDay;
        CCheckpointServices checkpointsService(testSetup.checkpoint_data());
        double initialTxs = static_cast<double>(sharedBlockIndex->nChainTx);
        double estimateOfTotalTransactions = transactionsAtCheckpoint + static_cast<double>(transactionsPerDay*numberOfDays);

        double progress = checkpointsService.GuessVerificationProgress(sharedBlockIndex.get(),false);

        BOOST_CHECK_CLOSE(progress, initialTxs/estimateOfTotalTransactions, 0.01);
    }
}

BOOST_AUTO_TEST_SUITE_END()
