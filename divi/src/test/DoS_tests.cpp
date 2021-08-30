// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//

#include <amount.h>
#include <chainparams.h>
#include "keystore.h"
#include "net.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include <Settings.h>
#include <stdint.h>
#include <utiltime.h>
#include <main.h>
#include <OrphanTransactions.h>
#include <primitives/transaction.h>
#include <PeerBanningService.h>
#include <NodeStateRegistry.h>
#include <Node.h>
#include <random.h>
#include <NodeStateRegistry.h>
#include <SocketChannel.h>


#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

#include "test_only.h"

// Tests this internal-to-main.cpp method:
extern Settings& settings;

CService ToIP(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}
static SocketChannel& InvalidSocketChannel()
{
    static SocketChannel invalidChannel(INVALID_SOCKET);
    return invalidChannel;
}
struct DoSTestFixture
{
    DoSTestFixture()
    {
        constexpr int64_t singleDayDurationInSeconds = 24*60*60;
        PeerBanningService::SetDefaultBanDuration(singleDayDurationInSeconds);
    }
    ~DoSTestFixture()
    {
        PeerBanningService::SetDefaultBanDuration(0);
    }
};

BOOST_FIXTURE_TEST_SUITE(DoS_tests,DoSTestFixture)

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    CNodeSignals& nodeSignals = GetNodeSignals();
    PeerBanningService::ClearBanned();
    CAddress addr1(ToIP(0xa0b0c001));
    CNode dummyNode1(InvalidSocketChannel(),&nodeSignals,GetNetworkAddressManager(), addr1, "", true,false);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetNodeState(), 100); // Should get banned
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr1));
    BOOST_CHECK(!PeerBanningService::IsBanned(GetTime(),ToIP(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ToIP(0xa0b0c002));
    CNode dummyNode2(InvalidSocketChannel(),&nodeSignals,GetNetworkAddressManager(), addr2, "", true,false);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetNodeState(), 50);
    SendMessages(&dummyNode2, false);
    BOOST_CHECK(!PeerBanningService::IsBanned(GetTime(),addr2)); // 2 not banned yet...
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr1));  // ... but 1 still should be
    Misbehaving(dummyNode2.GetNodeState(), 50);
    SendMessages(&dummyNode2, false);
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr2));
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    CNodeSignals& nodeSignals = GetNodeSignals();
    PeerBanningService::ClearBanned();
    settings.SetParameter("-banscore", "111"); // because 11 is my favorite number
    CAddress addr1(ToIP(0xa0b0c001));
    CNode dummyNode1(InvalidSocketChannel(),&nodeSignals,GetNetworkAddressManager(), addr1, "", true,false);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetNodeState(), 100);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(!PeerBanningService::IsBanned(GetTime(),addr1));
    Misbehaving(dummyNode1.GetNodeState(), 10);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(!PeerBanningService::IsBanned(GetTime(),addr1));
    Misbehaving(dummyNode1.GetNodeState(), 1);
    SendMessages(&dummyNode1, false);
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr1));
    settings.ForceRemoveArg("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    CNodeSignals& nodeSignals = GetNodeSignals();
    PeerBanningService::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ToIP(0xa0b0c001));
    CNode dummyNode(InvalidSocketChannel(),&nodeSignals,GetNetworkAddressManager(), addr, "", true,false);
    dummyNode.nVersion = 1;

    Misbehaving(dummyNode.GetNodeState(), 100);
    SendMessages(&dummyNode, false);
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr));

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(PeerBanningService::IsBanned(GetTime(),addr));

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!PeerBanningService::IsBanned(GetTime(),addr));
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{



    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = SelectRandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        SignSignature(keystore, txPrev, tx, 0);

        AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransaction txPrev = SelectRandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        SignSignature(keystore, txPrev, tx, 0);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        BOOST_CHECK(!AddOrphanTx(tx, i));
    }

    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        size_t sizeBefore = OrphanTotalCount();
        EraseOrphansFor(i);
        BOOST_CHECK(OrphanTotalCount() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    LimitOrphanTxSize(40);
    BOOST_CHECK(OrphanTotalCount() <= 40);
    LimitOrphanTxSize(10);
    BOOST_CHECK(OrphanTotalCount() <= 10);
    LimitOrphanTxSize(0);
    BOOST_CHECK(OrphanMapsAreEmpty());

}

BOOST_AUTO_TEST_SUITE_END()
