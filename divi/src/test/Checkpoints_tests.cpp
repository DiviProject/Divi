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
#define SKIP_TEST *boost::unit_test::disabled()
using namespace std;

class TestCase
{
private:
    int maxNumberOfBlocks = 800000;
public:
    TestCase()
    {
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


BOOST_AUTO_TEST_SUITE_END()
