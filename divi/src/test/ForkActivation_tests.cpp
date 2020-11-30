// Copyright (c) 2020 The DIVI Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test_only.h"

#include "ForkActivation.h"
#include "chain.h"

#include <cassert>
#include <memory>
#include <vector>

namespace
{

/**
 * Test fixture for the ActivationState.  It keeps track of a local
 * dummy "chain" (just CBlockIndex instances), to which the tests can
 * add new blocks with data as needed and then check the activation
 * status against it.
 */
class ForkActivationTestContainer
{

private:

  /**
   * The CIndex entries we've built up for our test.  The last one
   * will correspond to the "current tip".
   */
  std::vector<std::unique_ptr<CBlockIndex>> blocks;

public:

  ForkActivationTestContainer()
  {
    /* We always start with a genesis block of version 1, just as
       in the real blockchain.  */
    AddBlock()->nVersion = 1;
  }

  /**
   * Adds a new block to the chain and returns it for setting
   * data as needed.
   */
  CBlockIndex*
  AddBlock()
  {
    CBlockIndex* pprev = nullptr;
    if (!blocks.empty())
      pprev = blocks.back().get();

    CBlockIndex* res = new CBlockIndex();
    res->pprev = pprev;
    res->nVersion = 4;
    res->phashBlock = new uint256(res->GetBlockHeader().GetHash());

    blocks.emplace_back(res);
    return res;
  }

  /**
   * Constructs an activation state instance based on the current chain,
   * constructed with context (the CBlockIndex).
   */
  ActivationState
  FromContext() const
  {
    assert(!blocks.empty());
    return ActivationState(blocks.back().get());
  }

  /**
   * Constructs an activation state instance based on the current chain,
   * constructed just from the top block without context.
   */
  ActivationState
  FromBlock() const
  {
    assert(!blocks.empty());
    return ActivationState(blocks.back()->GetBlockHeader());
  }

};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(ActivationState_tests, ForkActivationTestContainer)

BOOST_AUTO_TEST_CASE(activationByTimestamp)
{
  /* Add some blocks with an early timestamp.  */
  for (int i = 0; i < 20; ++i)
    AddBlock()->nTime = 999999999;

  /* One block beyond the timestamp is enough to activate the fork for it.  */
  AddBlock()->nTime = 1000000000;
  BOOST_CHECK_EQUAL(FromBlock().IsActive(Fork::TestByTimestamp), true);
  BOOST_CHECK_EQUAL(FromContext().IsActive(Fork::TestByTimestamp), true);

  /* The fork can temporarily get back to inactive as well (although that
     won't be possible permanently in practice since the median time will
     at some point enforce a block time beyond the activation).  */
  for (int i = 0; i < 20; ++i)
    AddBlock()->nTime = 1000000000;
  AddBlock()->nTime = 999999999;
  BOOST_CHECK_EQUAL(FromBlock().IsActive(Fork::TestByTimestamp), false);
  BOOST_CHECK_EQUAL(FromContext().IsActive(Fork::TestByTimestamp), false);
}

BOOST_AUTO_TEST_SUITE_END()
