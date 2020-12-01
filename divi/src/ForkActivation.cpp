// Copyright (c) 2020 The DIVI Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ForkActivation.h"

#include "chain.h"
#include "primitives/block.h"

#include <unordered_map>

namespace
{
constexpr int64_t unixTimestampForDec31stMidnight = 1609459199;
/**
 * For forks that get activated at a certain block time, the associated
 * activation times.
 */
const std::unordered_map<Fork, int64_t> ACTIVATION_TIMES = {
  /* FIXME: Set real activation height for staking vaults once
     the schedule has been finalised.  */
  {Fork::StakingVaults, 2000000000},
  {Fork::TestByTimestamp, 1000000000},
  {Fork::HardenedStakeModifier, unixTimestampForDec31stMidnight},
  {Fork::UniformLotteryWinners, unixTimestampForDec31stMidnight},
};

} // anonymous namespace

ActivationState::ActivationState(const CBlockIndex* pi)
  : nTime(pi->nTime)
{}

ActivationState::ActivationState(const CBlockHeader& block)
  : nTime(block.nTime)
{}

bool ActivationState::IsActive(const Fork f) const
{
  const auto mit = ACTIVATION_TIMES.find(f);
  assert(mit != ACTIVATION_TIMES.end());
  return nTime >= mit->second;
}
