#include <test/test_only.h>

#include <hash.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

namespace {

uint256
HashStr (const std::string& str)
{
  return Hash (str.begin (), str.end ());
}

std::vector<unsigned char>
HashToVec (const std::string& str)
{
    const uint256 val = HashStr (str);
    return std::vector<unsigned char> (val.begin (), val.end ());
}

class BareTxidTestFixture
{

public:

  /* Just a "normal" transaction with non-trivial data, inputs and outputs,
     in both read-only and mutable form.  */
  CMutableTransaction mtx;
  CTransaction tx;

  BareTxidTestFixture ()
  {
    mtx.nVersion = 1;
    mtx.nLockTime = 1234567890;

    mtx.vin.emplace_back (HashStr ("in 1"), 4, CScript () << OP_TRUE << OP_FALSE);
    mtx.vin.emplace_back (HashStr ("in 2"), 2, CScript () << OP_DUP);

    mtx.vout.emplace_back (1 * COIN, CScript () << OP_TRUE);
    mtx.vout.emplace_back (0, CScript () << OP_META << HashToVec ("data"));

    tx = CTransaction (mtx);
  }

  /** Helper function that returns the bare txid of mtx.  */
  uint256
  GetMtxBareTxid () const
  {
    return CTransaction (mtx).GetBareTxid ();
  }

};

BOOST_FIXTURE_TEST_SUITE (BareTxid_tests, BareTxidTestFixture)

BOOST_AUTO_TEST_CASE (commitsToTxVersion)
{
  mtx.nVersion = 42;
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToLockTime)
{
  mtx.nLockTime = 100;
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToInputs)
{
  mtx.vin.emplace_back ();
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToInputPrevout)
{
  mtx.vin[0].prevout.n = 42;
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToInputSequence)
{
  mtx.vin[0].nSequence = 123;
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToOutputs)
{
  mtx.vout.emplace_back ();
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToOutputValue)
{
  mtx.vout[0].nValue = 20 * COIN;
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (commitsToOutputScript)
{
  mtx.vout[1].scriptPubKey = CScript () << OP_META << HashToVec ("other data");
  BOOST_CHECK (GetMtxBareTxid () != tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (doesNotCommitToInputSignature)
{
  mtx.vin[0].scriptSig.clear ();
  mtx.vin[1].scriptSig = CScript () << OP_META << HashToVec ("foo");
  BOOST_CHECK (GetMtxBareTxid () == tx.GetBareTxid ());
}

BOOST_AUTO_TEST_CASE (equalsTxidForCoinbase)
{
  mtx.vin.resize (1);
  mtx.vin[0].prevout.SetNull ();

  const CTransaction tx1(mtx);
  BOOST_CHECK (tx1.IsCoinBase ());

  mtx.vin[0].scriptSig.clear ();
  const CTransaction tx2(mtx);

  BOOST_CHECK (tx1.GetBareTxid () == tx1.GetHash ());
  BOOST_CHECK (tx1.GetBareTxid () != tx2.GetBareTxid ());
}

BOOST_AUTO_TEST_SUITE_END ()

} // anonymous namespace
