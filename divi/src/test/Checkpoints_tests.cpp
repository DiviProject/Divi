// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

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
{
    {   // Exhaustive search for correctness
        unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
        TestCase testSetup(checkpointCount);
        CCheckpoints checkpointsService( testSetup.checkpoint_data() );

        for(const auto& checkpoint: testSetup.mapCheckpoints_)
        {
            BOOST_CHECK(checkpointsService.CheckBlock(checkpoint.first,checkpoint.second));
        }
    }
}

BOOST_AUTO_TEST_CASE(randomCheckpointsWillBeCorrectlyHandled)
{
    {   // Exhaustive search for correctness
        unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
        TestCase testSetup(checkpointCount);
        CCheckpoints checkpointsService( testSetup.checkpoint_data() );

        for(unsigned checkpointIndex = 0; checkpointIndex < checkpointCount; checkpointIndex++)
        {
            std::pair<std::pair<int, uint256>,bool> checkpointAndExpectation = testSetup.getRandomCheckpointAndExpectation();
            std::pair<int, uint256> checkpoint = checkpointAndExpectation.first;
            BOOST_CHECK(checkpointsService.CheckBlock(checkpoint.first,checkpoint.second) == checkpointAndExpectation.second);
        }
    }
}

BOOST_AUTO_TEST_CASE(deliberatlyIncorrectCheckpointsWillBeCorrectlyHandled)
{
    {   // Exhaustive search for correctness
        unsigned checkpointCount = static_cast<unsigned>(abs(GetRandInt(25)))+10u;
        TestCase testSetup(checkpointCount);
        CCheckpoints checkpointsService( testSetup.checkpoint_data() );

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
}

BOOST_AUTO_TEST_SUITE_END()
