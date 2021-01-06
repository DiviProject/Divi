// Copyright (c) 2020 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "BlockRewards.h"
#include "coins.h"
#include "hash.h"
#include "kernel.h"
#include "script/StakingVaultScript.h"
#include "utilstrencodings.h"

#include <boost/test/unit_test.hpp>
#include "test_only.h"

namespace
{

/**
 * Fixture for tests of CheckCoinstakeForVaults.  It mostly sets up
 * a coins view with some example coins in it and an example
 * vault script.
 */
class CheckCoinstakeForVaultsTestFixture
{

private:

  CCoinsView coinsDummy;
  CCoinsViewCache coins;

  const CBlockRewards rewards;

protected:

  CScript scriptVault;
  CScript scriptOtherVault;
  CScript scriptNonVault;

  std::vector<COutPoint> vaultCoins;
  std::vector<COutPoint> nonVaultCoins;

  CheckCoinstakeForVaultsTestFixture()
    : coins(&coinsDummy), rewards(CENT, 0, 0, 0, 0, 0)
  {
    const std::vector<unsigned char> key1(20, 'x');
    const std::vector<unsigned char> key2(20, 'y');
    const std::vector<unsigned char> key3(20, 'z');

    scriptVault = CreateStakingVaultScript(key1, key2);
    scriptOtherVault = CreateStakingVaultScript(key3, key2);
    scriptNonVault = CScript() << OP_TRUE;

    BOOST_CHECK(IsStakingVaultScript(scriptVault));
    BOOST_CHECK(IsStakingVaultScript(scriptOtherVault));
    BOOST_CHECK(!IsStakingVaultScript(scriptNonVault));

    CMutableTransaction dummyTxVault;
    CMutableTransaction dummyTxNonVault;
    dummyTxVault.vout.resize(2);
    dummyTxNonVault.vout.resize(dummyTxVault.vout.size());
    for (unsigned i = 0; i < dummyTxVault.vout.size(); ++i) {
      dummyTxVault.vout[i].nValue = 15000 * COIN;
      dummyTxVault.vout[i].scriptPubKey = scriptVault;
      dummyTxNonVault.vout[i].nValue = 15000 * COIN;
      dummyTxNonVault.vout[i].scriptPubKey = scriptNonVault;
    }

    coins.ModifyCoins(dummyTxVault.GetHash())->FromTx(dummyTxVault, 0);
    coins.ModifyCoins(dummyTxNonVault.GetHash())->FromTx(dummyTxNonVault, 0);

    for (unsigned i = 0; i < dummyTxVault.vout.size(); ++i) {
      vaultCoins.emplace_back(dummyTxVault.GetHash(), i);
      nonVaultCoins.emplace_back(dummyTxNonVault.GetHash(), i);
    }
  }

  bool RunCheck(const CMutableTransaction& mtx)
  {
    return CheckCoinstakeForVaults(CTransaction(mtx), rewards, coins);
  }

};

BOOST_FIXTURE_TEST_SUITE(CheckCoinstakeForVaults_tests, CheckCoinstakeForVaultsTestFixture)

BOOST_AUTO_TEST_CASE(willIgnoreNonCoinstakeTransactions)
{
  CMutableTransaction mtx;
  mtx.vin.emplace_back(vaultCoins[0]);
  mtx.vout.emplace_back ();
  mtx.vout[0].SetEmpty ();
  mtx.vout.emplace_back(15000 * COIN + CENT, scriptNonVault);

  /* As a coinstake, this is an invalid transaction.  */
  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));

  /* But if it is not a coinstake, then the check function will always
     accept it no matter what.  */
  mtx.vout[0] = CTxOut (1, scriptNonVault);
  BOOST_CHECK(!CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willAllowSplittingOfInputPlusRewardIntoTwoOutputs)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(nonVaultCoins[0]));
  mtx.vin.push_back(CTxIn(nonVaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(5000 * COIN, scriptNonVault));
  mtx.vout.push_back(CTxOut(25000 * COIN, scriptNonVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willAllowCorrectVaultPayment)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(30000 * COIN + CENT, scriptVault));
  mtx.vout.push_back(CTxOut(CENT, scriptNonVault));
  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));

  mtx.vout[1].nValue += 1;
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willNotAllowPaymentToNonVault)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(15000 * COIN + CENT, scriptNonVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(15000 * COIN + CENT, scriptOtherVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willDisallowVaultToUnderpay)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(15000 * COIN + CENT, scriptVault));
  mtx.vout.push_back(CTxOut(15000 * COIN, scriptOtherVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));

  mtx.vout[1].nValue += 15000 * COIN;
  mtx.vout.pop_back();
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willAllowStakingOutputToBeSplit)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(10000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(20000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willNotAllowStakingOutputToBeSplitTooOften)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(10000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(10000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(10000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willNotAllowStakingOutputToBeSplitTooSmall)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(vaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(25000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(5000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(5000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(25000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));
}

BOOST_AUTO_TEST_CASE(willRequireStakingVaultToAbsorbNonVaultFunds)
{
  CMutableTransaction mtx;
  mtx.vin.push_back(CTxIn(nonVaultCoins[0]));
  mtx.vin.push_back(CTxIn(vaultCoins[1]));

  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(10000 * COIN, scriptNonVault));
  mtx.vout.push_back(CTxOut(20000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(!RunCheck(mtx));

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(10000 * COIN, scriptVault));
  mtx.vout.push_back(CTxOut(20000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut());
  mtx.vout[0].SetEmpty();
  mtx.vout.push_back(CTxOut(30000 * COIN + CENT, scriptVault));

  BOOST_CHECK(CTransaction(mtx).IsCoinStake());
  BOOST_CHECK(RunCheck(mtx));
}

BOOST_AUTO_TEST_SUITE_END()

} // anonymous namespace
