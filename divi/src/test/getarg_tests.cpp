// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

#include <Settings.h>

struct SettingsTestContainer
{

    std::map<std::string, std::string> mapArgs;
    std::map<std::string, std::vector<std::string> > mapMultiArgs;
    CopyableSettings settings;
    

    SettingsTestContainer(
        ): mapArgs()
        , mapMultiArgs()
        , settings(mapArgs, mapMultiArgs)
    {

    }
};

BOOST_FIXTURE_TEST_SUITE(getarg_tests, SettingsTestContainer)

static void ResetArgs(CopyableSettings& settings, const std::string& commandLineArguments)
{
    std::vector<std::string> vecArg;
    if (commandLineArguments.size())
      boost::split(vecArg, commandLineArguments, boost::is_space(), boost::token_compress_on);

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testbitcoin");

    // Convert to char*:
    std::vector<const char*> vecChar;
    BOOST_FOREACH(std::string& s, vecArg)
        vecChar.push_back(s.c_str());

    settings.ParseParameters(vecChar.size(), &vecChar[0]);
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    ResetArgs(settings, "-foo");
    BOOST_CHECK(settings.GetBoolArg("-foo", false));
    BOOST_CHECK(settings.GetBoolArg("-foo", true));

    BOOST_CHECK(!settings.GetBoolArg("-fo", false));
    BOOST_CHECK(settings.GetBoolArg("-fo", true));

    BOOST_CHECK(!settings.GetBoolArg("-fooo", false));
    BOOST_CHECK(settings.GetBoolArg("-fooo", true));

    ResetArgs( settings, "-foo=0");
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "-foo=1");
    BOOST_CHECK(settings.GetBoolArg("-foo", false));
    BOOST_CHECK(settings.GetBoolArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs( settings, "-nofoo");
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "-nofoo=1");
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "-foo -nofoo");  // -nofoo should win
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "-foo=1 -nofoo=1");  // -nofoo should win
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "-foo=0 -nofoo=0");  // -nofoo=0 should win
    BOOST_CHECK(settings.GetBoolArg("-foo", false));
    BOOST_CHECK(settings.GetBoolArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs( settings, "--foo=1");
    BOOST_CHECK(settings.GetBoolArg("-foo", false));
    BOOST_CHECK(settings.GetBoolArg("-foo", true));

    ResetArgs( settings, "--nofoo=1");
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));

}

BOOST_AUTO_TEST_CASE(stringarg)
{
    ResetArgs( settings, "");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", "eleven"), "eleven");

    ResetArgs( settings, "-foo -bar");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", "eleven"), "");

    ResetArgs( settings, "-foo=");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", "eleven"), "");

    ResetArgs( settings, "-foo=11");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "11");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", "eleven"), "11");

    ResetArgs( settings, "-foo=eleven");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "eleven");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", "eleven"), "eleven");

}

BOOST_AUTO_TEST_CASE(intarg)
{
    ResetArgs( settings, "");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", 11), 11);
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", 0), 0);

    ResetArgs( settings, "-foo -bar");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", 11), 0);
    BOOST_CHECK_EQUAL(settings.GetArg("-bar", 11), 0);

    ResetArgs( settings, "-foo=11 -bar=12");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", 0), 11);
    BOOST_CHECK_EQUAL(settings.GetArg("-bar", 11), 12);

    ResetArgs( settings, "-foo=NaN -bar=NotANumber");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", 1), 0);
    BOOST_CHECK_EQUAL(settings.GetArg("-bar", 11), 0);
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    ResetArgs( settings, "--foo");
    BOOST_CHECK_EQUAL(settings.GetBoolArg("-foo", false), true);

    ResetArgs( settings, "--foo=verbose --bar=1");
    BOOST_CHECK_EQUAL(settings.GetArg("-foo", ""), "verbose");
    BOOST_CHECK_EQUAL(settings.GetArg("-bar", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno)
{
    ResetArgs( settings, "-nofoo");
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));

    ResetArgs( settings, "-nofoo=1");
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));

    ResetArgs( settings, "-nofoo=0");
    BOOST_CHECK(settings.GetBoolArg("-foo", true));
    BOOST_CHECK(settings.GetBoolArg("-foo", false));

    ResetArgs( settings, "-foo --nofoo"); // --nofoo should win
    BOOST_CHECK(!settings.GetBoolArg("-foo", true));
    BOOST_CHECK(!settings.GetBoolArg("-foo", false));

    ResetArgs( settings, "-nofoo -foo"); // foo always wins:
    BOOST_CHECK(settings.GetBoolArg("-foo", true));
    BOOST_CHECK(settings.GetBoolArg("-foo", false));
}

BOOST_AUTO_TEST_SUITE_END()
