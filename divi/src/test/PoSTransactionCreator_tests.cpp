// Copyright (c) 2021 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "PoSTransactionCreator.h"

#include "chain.h"
#include "chainparams.h"
#include "ProofOfStakeModule.h"
#include "script/standard.h"
#include "Settings.h"
#include "WalletTx.h"

#include "test/FakeBlockIndexChain.h"
#include "test/FakeWallet.h"
#include "test/MockBlockIncentivesPopulator.h"
#include "test/MockBlockSubsidyProvider.h"

#include <boost/test/unit_test.hpp>
#include "test/test_only.h"

#include <gmock/gmock.h>

#include <map>

extern Settings& settings;

namespace
{

using testing::_;
using testing::AtLeast;
using testing::Return;

class PoSTransactionCreatorTestFixture
{

protected:

  FakeBlockIndexWithHashes fakeChain;
  FakeWallet fakeWallet;

private:

  const CChainParams& chainParams;

  MockBlockIncentivesPopulator blockIncentivesPopulator;
  MockBlockSubsidyProvider blockSubsidyProvider;
  ProofOfStakeModule posModule;

  std::map<unsigned, unsigned> hashedBlockTimestamps;

  PoSTransactionCreator txCreator;

protected:

  /** A script from the wallet for convenience.  */
  const CScript walletScript;

  /* Convenience variable for the wallet's AddDefaultTx.  */
  unsigned outputIndex;

  PoSTransactionCreatorTestFixture()
    : fakeChain(1, 1600000000, 1)
    , fakeWallet(fakeChain)
    , chainParams(
        Params(CBaseChainParams::REGTEST))
    , posModule(chainParams, *fakeChain.activeChain, *fakeChain.blockIndexByHash)
    , txCreator(
        settings,
        chainParams,
        *fakeChain.activeChain,
        *fakeChain.blockIndexByHash,
        blockSubsidyProvider,
        blockIncentivesPopulator,
        posModule.proofOfStakeGenerator(),
        hashedBlockTimestamps)
    , walletScript(GetScriptForDestination(fakeWallet.getNewKey().GetID()))
  {
    txCreator.setWallet(fakeWallet.getWallet());
    /* Set up a default block reward if we don't need anything else.  */
    EXPECT_CALL(blockSubsidyProvider, GetBlockSubsidity(_))
        .WillRepeatedly(Return(CBlockRewards(10 * COIN, COIN, 0, 0, 0, 0)));

    /* We don't care about the block payments.  */
    EXPECT_CALL(blockIncentivesPopulator, FillBlockPayee(_, _, _)).Times(AtLeast(0));
    EXPECT_CALL(blockIncentivesPopulator, IsBlockValueValid(_, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(blockIncentivesPopulator, HasValidPayees(_, _))
        .WillRepeatedly(Return(true));
  }

  /** Calls CreateProofOfStake on our PoSTransactionCreator with the
   *  fake wallet's chain tip and regtest difficulty.  */
  bool CreatePoS()
  {
    CBlock block;
    block.vtx.resize(2);
    block.nBits = 0x207fffff;
    return txCreator.attachBlockProof(fakeChain.activeChain->Tip(), block);
  }
};

BOOST_FIXTURE_TEST_SUITE(PoSTransactionCreator_tests, PoSTransactionCreatorTestFixture)

BOOST_AUTO_TEST_CASE(failsWithoutCoinsInWallet)
{
  BOOST_CHECK(!CreatePoS());
}

BOOST_AUTO_TEST_CASE(checksForConfirmationsAndAge)
{
  const auto& tx = fakeWallet.AddDefaultTx(walletScript, outputIndex, 1000 * COIN);
  fakeWallet.FakeAddToChain(tx);

  BOOST_CHECK(!CreatePoS());

  fakeWallet.AddConfirmations(20, 1000);
  BOOST_CHECK(CreatePoS());
}

BOOST_AUTO_TEST_SUITE_END()

} // anonymous namespace
