// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparamsbase.h"

#include "util.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

/**
* Main network
*/
class CBaseMainParams : public CBaseChainParams
{
public:
	CBaseMainParams()
	{
		networkID = CBaseChainParams::MAIN;
		nRPCPort = 51473;
	}
};
static CBaseMainParams mainParams;

/**
* Beta network
*/
class CBaseBetaParams : public CBaseChainParams
{
public:
	CBaseBetaParams()
	{
		networkID = CBaseChainParams::BETATEST;
		nRPCPort = 51473;
	}
};
static CBaseMainParams betaParams;

/**
 * Testnet (v3)
 */
class CBaseTestNetParams : public CBaseMainParams
{
public:
    CBaseTestNetParams()
    {
        networkID = CBaseChainParams::TESTNET;
        nRPCPort = 51475;
        strDataDir = "testnet4";
    }
};
static CBaseTestNetParams testNetParams;

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseTestNetParams
{
public:
    CBaseRegTestParams()
    {
        networkID = CBaseChainParams::REGTEST;
        strDataDir = "regtest";
    }
};
static CBaseRegTestParams regTestParams;

/*
 * Unit test
 */
class CBaseUnitTestParams : public CBaseMainParams
{
public:
    CBaseUnitTestParams()
    {
        networkID = CBaseChainParams::UNITTEST;
        strDataDir = "unittest";
    }
};
static CBaseUnitTestParams unitTestParams;

static CBaseChainParams* pCurrentBaseParams = 0;

const CBaseChainParams& BaseParams()
{
    assert(pCurrentBaseParams);
    return *pCurrentBaseParams;
}

void SelectBaseParams(CBaseChainParams::Network network)
{
    switch (network) {
    case CBaseChainParams::MAIN:
		pCurrentBaseParams = &mainParams;
        break;
    case CBaseChainParams::TESTNET:
        pCurrentBaseParams = &testNetParams;
        break;
    case CBaseChainParams::REGTEST:
        pCurrentBaseParams = &regTestParams;
        break;
	case CBaseChainParams::UNITTEST:
		pCurrentBaseParams = &mainParams;
		break;
	case CBaseChainParams::BETATEST:
		pCurrentBaseParams = &betaParams;
		break;
	default:
        assert(false && "Unimplemented network");
        return;
    }
}

CBaseChainParams::Network NetworkIdFromCommandLine()
{
    bool fRegTest = GetBoolArg("-regtest", false);
    // bool fTestNet = GetBoolArg("-testnet", false);
	bool fTestNet = false;

    if (fTestNet && fRegTest)
        return CBaseChainParams::MAX_NETWORK_TYPES;
    if (fRegTest)
        return CBaseChainParams::REGTEST;
    if (fTestNet)
        return CBaseChainParams::TESTNET;
	if (GetBoolArg("-testnet", false))
		return CBaseChainParams::BETATEST;
    return CBaseChainParams::MAIN;
}

bool SelectBaseParamsFromCommandLine()
{
	CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectBaseParams(network);
    return true;
}

bool AreBaseParamsConfigured()
{
    return pCurrentBaseParams != NULL;
}
