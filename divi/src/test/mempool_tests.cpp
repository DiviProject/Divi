// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"

#include "FakeBlockIndexChain.h"

#include <boost/test/unit_test.hpp>
#include <list>

class MempoolTestFixture
{

private:

  CCoinsView coinsDummy;

protected:

  /* The test mempool will use these flags instead of the global ones.  */
  bool addressIndex = false;
  bool spentIndex = false;

  /** A parent transaction.  */
  CMutableTransaction txParent;

  /** Three children of the parent.  */
  CMutableTransaction txChild[3];

  /** Three grand children.  */
  CMutableTransaction txGrandChild[3];

  /** Our fake blockchain.  */
  FakeBlockIndexWithHashes fakeChain;

  /** The test mempool instance.  */
  CTxMemPool testPool;

  /** Coins view with our test mempool.  */
  CCoinsViewMemPool coinsMemPool;

  /** A coins view with the confirmed parent input and the mempool.  */
  CCoinsViewCache coins;

public:

  MempoolTestFixture()
    : fakeChain(1, 1500000000, 1),
      testPool(CFeeRate(0), addressIndex, spentIndex),
      coinsMemPool(&coinsDummy, testPool), coins(&coinsMemPool)
  {
    CMutableTransaction mtx;
    mtx.vout.emplace_back(2 * COIN, CScript () << OP_TRUE);
    mtx.vout.emplace_back(COIN, CScript () << OP_TRUE);
    coins.ModifyCoins(mtx.GetHash())->FromTx(mtx, 0);

    coins.SetBestBlock(fakeChain.activeChain->Tip()->GetBlockHash());

    txParent.vin.resize(2);
    txParent.vin[0].prevout = COutPoint(mtx.GetHash(), 0);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    /* Add a second input to make sure the transaction does not qualify as
       coinbase and thus has a bare txid unequal to its normal hash.  */
    txParent.vin[1].prevout = COutPoint(mtx.GetHash(), 1);
    txParent.vin[1].scriptSig = CScript() << OP_12;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++)
    {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = COIN;
    }
    assert(txParent.GetHash() != txParent.GetBareTxid());

    for (int i = 0; i < 3; i++)
    {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout.hash = txParent.GetHash();
        txChild[i].vin[0].prevout.n = i;
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = COIN;
    }

    for (int i = 0; i < 3; i++)
    {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout.hash = txChild[i].GetHash();
        txGrandChild[i].vin[0].prevout.n = 0;
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = COIN;
    }

    testPool.setSanityCheck(true);
    testPool.clear();
  }

  /** Adds the parent, childs and grandchilds to the mempool.  */
  void AddAll()
  {
      testPool.addUnchecked(txParent.GetHash(), CTxMemPoolEntry(txParent, 0, 0, 0.0, 1), coins);
      for (int i = 0; i < 3; i++)
      {
          testPool.addUnchecked(txChild[i].GetHash(), CTxMemPoolEntry(txChild[i], 0, 0, 0.0, 1), coins);
          testPool.addUnchecked(txGrandChild[i].GetHash(), CTxMemPoolEntry(txGrandChild[i], 0, 0, 0.0, 1), coins);
      }
  }

};

BOOST_FIXTURE_TEST_SUITE(mempool_tests, MempoolTestFixture)

BOOST_AUTO_TEST_CASE(MempoolRemoveTest)
{
    // Test CTxMemPool::remove functionality

    std::list<CTransaction> removed;

    // Nothing in pool, remove should do nothing:
    testPool.remove(txParent, removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 0);

    // Just the parent:
    testPool.addUnchecked(txParent.GetHash(), CTxMemPoolEntry(txParent, 0, 0, 0.0, 1), coins);
    testPool.remove(txParent, removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 1);
    removed.clear();
    
    // Parent, children, grandchildren:
    AddAll();

    testPool.check(&coins, *fakeChain.blockIndexByHash);

    // Remove Child[0], GrandChild[0] should be removed:
    testPool.remove(txChild[0], removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 2);
    removed.clear();
    // ... make sure grandchild and child are gone:
    testPool.remove(txGrandChild[0], removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 0);
    testPool.remove(txChild[0], removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 0);
    // Remove parent, all children/grandchildren should go:
    testPool.remove(txParent, removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 5);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removed.clear();

    // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), CTxMemPoolEntry(txChild[i], 0, 0, 0.0, 1), coins);
        testPool.addUnchecked(txGrandChild[i].GetHash(), CTxMemPoolEntry(txGrandChild[i], 0, 0, 0.0, 1), coins);
    }
    // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
    // put into the mempool (maybe because it is non-standard):
    testPool.remove(txParent, removed, true);
    BOOST_CHECK_EQUAL(removed.size(), 6);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
    removed.clear();
}

BOOST_AUTO_TEST_CASE(MempoolDirectLookup)
{
    CTransaction tx;
    std::list<CTransaction> removed;

    AddAll();

    BOOST_CHECK(testPool.lookupBareTxid(txParent.GetBareTxid(), tx));
    BOOST_CHECK(tx.GetHash() == txParent.GetHash());
    BOOST_CHECK(testPool.lookup(txParent.GetHash(), tx));
    BOOST_CHECK(tx.GetHash() == txParent.GetHash());

    BOOST_CHECK(!testPool.lookup(txParent.GetBareTxid(), tx));
    BOOST_CHECK(!testPool.lookupBareTxid(txParent.GetHash(), tx));

    testPool.remove(txParent, removed, true);
    BOOST_CHECK(!testPool.lookup(txParent.GetHash(), tx));
    BOOST_CHECK(!testPool.lookup(txChild[0].GetHash(), tx));
    BOOST_CHECK(!testPool.lookup(txGrandChild[0].GetHash(), tx));
    BOOST_CHECK(!testPool.lookupBareTxid(txParent.GetBareTxid(), tx));
    BOOST_CHECK(!testPool.lookupBareTxid(txChild[0].GetBareTxid(), tx));
    BOOST_CHECK(!testPool.lookupBareTxid(txGrandChild[0].GetBareTxid(), tx));
}

BOOST_AUTO_TEST_CASE(MempoolOutpointLookup)
{
    CTransaction tx;
    CCoins c;

    AddAll();
    CCoinsViewMemPool viewPool(&coins, testPool);

    BOOST_CHECK(testPool.lookupOutpoint(txParent.GetHash(), tx));
    BOOST_CHECK(!testPool.lookupOutpoint(txParent.GetBareTxid(), tx));
    BOOST_CHECK(testPool.lookupOutpoint(txChild[0].GetHash(), tx));
    BOOST_CHECK(!testPool.lookupOutpoint(txChild[0].GetBareTxid(), tx));

    BOOST_CHECK(viewPool.HaveCoins(txParent.GetHash()));
    BOOST_CHECK(viewPool.GetCoins(txParent.GetHash(), c));
    BOOST_CHECK(!viewPool.HaveCoins(txParent.GetBareTxid()));
    BOOST_CHECK(!viewPool.GetCoins(txParent.GetBareTxid(), c));

    BOOST_CHECK(viewPool.HaveCoins(txChild[0].GetHash()));
    BOOST_CHECK(viewPool.GetCoins(txChild[0].GetHash(), c));
    BOOST_CHECK(!viewPool.HaveCoins(txChild[0].GetBareTxid()));
    BOOST_CHECK(!viewPool.GetCoins(txChild[0].GetBareTxid(), c));
}

BOOST_AUTO_TEST_CASE(MempoolExists)
{
    CTransaction tx;
    std::list<CTransaction> removed;

    AddAll();

    BOOST_CHECK(testPool.exists(txParent.GetHash()));
    BOOST_CHECK(!testPool.exists(txParent.GetBareTxid()));
    BOOST_CHECK(!testPool.existsBareTxid(txParent.GetHash()));
    BOOST_CHECK(testPool.existsBareTxid(txParent.GetBareTxid()));

    testPool.remove(txParent, removed, true);
    BOOST_CHECK(!testPool.exists(txParent.GetHash()));
    BOOST_CHECK(!testPool.existsBareTxid(txParent.GetBareTxid()));
}

BOOST_AUTO_TEST_CASE(MempoolSpentIndex)
{
    spentIndex = true;

    testPool.addUnchecked(txParent.GetHash(), CTxMemPoolEntry(txParent, 0, 0, 0.0, 1), coins);
    testPool.addUnchecked(txChild[0].GetHash(), CTxMemPoolEntry(txChild[0], 0, 0, 0.0, 1), coins);

    const CSpentIndexKey keyParent(txParent.GetHash(), 0);
    const CSpentIndexKey keyChild(txChild[0].GetHash(), 0);

    CSpentIndexValue value;
    BOOST_CHECK(testPool.getSpentIndex(keyParent, value));
    BOOST_CHECK(value.txid == txChild[0].GetHash());
    BOOST_CHECK_EQUAL(value.inputIndex, 0);
    BOOST_CHECK(!testPool.getSpentIndex(keyChild, value));

    std::list<CTransaction> removed;
    testPool.remove(txChild[0], removed, true);

    BOOST_CHECK(!testPool.getSpentIndex(keyParent, value));
    BOOST_CHECK(!testPool.getSpentIndex(keyChild, value));
}

BOOST_AUTO_TEST_SUITE_END()
