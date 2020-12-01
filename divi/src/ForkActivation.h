// Copyright (c) 2020 The DIVI Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FORK_ACTIVATION_H
#define FORK_ACTIVATION_H

#include <cstdint>

class CBlockHeader;
class CBlockIndex;

/**
 * The list of consensus changes ("forks") that have been introduced
 * on the network since its launch and may need to be activated
 * (with conditional logic in other parts of the codebase depending
 * on whether or not a fork is active).
 */
enum class Fork
{

  /**
   * Staking vaults with SCRIPT_REQUIRE_COINSTAKE and a couple of other,
   * related changes.
   */
  StakingVaults,

  /* Test forks not actually deployed / active but used for unit tests.  */
  TestByTimestamp,
  HardenedStakeModifier,
  UniformLotteryWinners = HardenedStakeModifier,

};

/**
 * Activation state of forks.  Each instance corresponds to a particular block,
 * and can be used to query the state of each supported fork as it should be
 * for validating this block.
 */
class ActivationState
{

private:

  /** The timestamp of the block this is associated to.  */
  const int64_t nTime;

public:

  explicit ActivationState(const CBlockIndex* pi);
  explicit ActivationState(const CBlockHeader& block);

  ActivationState(ActivationState&&) = default;

  ActivationState() = delete;
  ActivationState(const ActivationState&) = delete;
  void operator=(const ActivationState&) = delete;

  /**
   * Returns true if the indicated fork should be considered active
   * for processing the associated block.
   */
  bool IsActive(Fork f) const;

};

#endif // FORK_ACTIVATION_H
