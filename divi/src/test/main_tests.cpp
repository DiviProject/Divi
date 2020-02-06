// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"
#include "main.h"

#include <boost/test/unit_test.hpp>

#include <iostream>
#include "test_only.h"
BOOST_AUTO_TEST_SUITE(main_tests)

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    SelectParams(CBaseChainParams::Network::MAIN);
    int numberOfBlocksPerHalving = Params().SubsidyHalvingInterval();
    CAmount nSum = 0;

    CAmount startingExpectedSubsidy = 1250 * COIN;
    CAmount yearlySubsidyReduction = 100 * COIN;
    CAmount minimumSubsidy = 250*COIN;
    auto expectedSubsidy = [startingExpectedSubsidy,yearlySubsidyReduction,minimumSubsidy](int year)->CAmount{
        return (year>1)? std::max(startingExpectedSubsidy - yearlySubsidyReduction*(year-1),minimumSubsidy): startingExpectedSubsidy;
    };

    for(int nHeight = 0; nHeight < numberOfBlocksPerHalving*13; ++nHeight)
    {
        CAmount nSubsidy = GetBlockSubsidity(nHeight).total();
        if(nHeight < 2)
        {
            BOOST_CHECK(nSubsidy == ((nHeight==0)? 50*COIN: Params().premineAmt) );
        }
        else
        {
            BOOST_CHECK_EQUAL(nSubsidy, expectedSubsidy(nHeight/numberOfBlocksPerHalving));
        }

        BOOST_CHECK( nSubsidy==0 || (nSum+nSubsidy > nSum));
        nSum += nSubsidy;
    }
}

BOOST_AUTO_TEST_SUITE_END()
