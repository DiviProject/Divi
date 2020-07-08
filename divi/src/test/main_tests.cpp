// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <boost/test/unit_test.hpp>

#include <iostream>
#include "test_only.h"

#include <chainparams.h>
#include <SuperblockHelpers.h>

BOOST_AUTO_TEST_SUITE(main_tests)


CAmount getExpectedSubsidyAtHeight(int nHeight, const CChainParams& chainParameters)
{
    CAmount startingExpectedSubsidy = 1250 * COIN;
    CAmount yearlySubsidyReduction = 100 * COIN;
    CAmount minimumSubsidy = 250*COIN;

    int numberOfBlocksPerHalving = chainParameters.SubsidyHalvingInterval();
    auto expectedSubsidy = [startingExpectedSubsidy,yearlySubsidyReduction,minimumSubsidy](int year)->CAmount{
        return (year>1)? std::max(startingExpectedSubsidy - yearlySubsidyReduction*(year-1),minimumSubsidy): startingExpectedSubsidy;
    };
    auto getTreasuryAndCharityContributions = [](CAmount legacySubsidyValue)
    {
        legacySubsidyValue -= 50*COIN;
        return legacySubsidyValue*17/100;
    };
    auto getLotteryContributions = [](CAmount legacySubsidyValue)
    {
        return 50*COIN;
    };

    CAmount expectedSubsidyValue = expectedSubsidy(nHeight/numberOfBlocksPerHalving);
    SuperblockSubsidyContainer superblockSubsidies(chainParameters);
    const I_SuperblockHeightValidator& heightValidator = superblockSubsidies.superblockHeightValidator();
    if(heightValidator.IsValidTreasuryBlockHeight(nHeight))
    {
        expectedSubsidyValue -= 50*COIN;
        expectedSubsidyValue = expectedSubsidyValue*83/100;
        for(int blockHeight = nHeight-1; blockHeight >= (nHeight - heightValidator.GetTreasuryBlockPaymentCycle(nHeight)); blockHeight--  )
        {
            expectedSubsidyValue += getTreasuryAndCharityContributions(expectedSubsidy(blockHeight/numberOfBlocksPerHalving));
        }
    }
    else if(heightValidator.IsValidLotteryBlockHeight(nHeight))
    {
        expectedSubsidyValue -= 50*COIN;
        expectedSubsidyValue = expectedSubsidyValue*83/100;
        for(int blockHeight = nHeight-1; blockHeight >= (nHeight - heightValidator.GetLotteryBlockPaymentCycle(nHeight)); blockHeight--  )
        {
            expectedSubsidyValue += getLotteryContributions(expectedSubsidy(blockHeight/numberOfBlocksPerHalving));
        }
    }
    else if(nHeight > chainParameters.LAST_POW_BLOCK())
    {
        expectedSubsidyValue -= 50*COIN;
        return expectedSubsidyValue*83/100;
    }
    return expectedSubsidyValue;
}

void CheckRewardDistribution(const CChainParams& chainParameters)
{
    CAmount nSum = 0;

    SuperblockSubsidyContainer subsidiesContainer(chainParameters);

    for(int nHeight = 0; nHeight < chainParameters.SubsidyHalvingInterval()*13; ++nHeight)
    {
        CAmount nSubsidy = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight).total();
        CAmount expectedSubsidyValue = CAmount(0);
        bool testPass = false;
        if(nHeight < 2)
        {
            expectedSubsidyValue = (nHeight==0)? 50*COIN: chainParameters.premineAmt;
            testPass = (nSubsidy == expectedSubsidyValue);
            BOOST_CHECK_MESSAGE(testPass, "Subsidy " << nSubsidy << " not equal to " << expectedSubsidyValue );
            if(!testPass) return;
        }
        else
        {
            expectedSubsidyValue = getExpectedSubsidyAtHeight(nHeight, chainParameters);

            testPass = nSubsidy==expectedSubsidyValue;
            BOOST_CHECK_MESSAGE(testPass, "Mismatched subsidy at height " << nHeight << "! " << nSubsidy << " vs. " << expectedSubsidyValue);
            if(!testPass) return;
        }

        BOOST_CHECK( nSubsidy==0 || (nSum+nSubsidy > nSum));
        nSum += nSubsidy;
    }
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    CheckRewardDistribution(Params(CBaseChainParams::Network::MAIN));
    CheckRewardDistribution(Params(CBaseChainParams::Network::TESTNET));
}

BOOST_AUTO_TEST_SUITE_END()
