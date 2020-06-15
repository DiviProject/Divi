#include <BlockRewards.h>

#include "utilmoneystr.h"
#include "tinyformat.h"

CBlockRewards::CBlockRewards(
    CAmount nStakeReward, 
    CAmount nMasternodeReward, 
    CAmount nTreasuryReward,
    CAmount nCharityReward, 
    CAmount nLotteryReward, 
    CAmount nProposalsReward
    ): nStakeReward(nStakeReward)
    , nMasternodeReward(nMasternodeReward)
    , nTreasuryReward(nTreasuryReward)
    , nCharityReward(nCharityReward)
    , nLotteryReward(nLotteryReward)
    , nProposalsReward(nProposalsReward)
{ }

CAmount CBlockRewards::total() const
{
    return nStakeReward + nMasternodeReward + nTreasuryReward + nCharityReward + nLotteryReward + nProposalsReward;
}

std::string CBlockRewards::ToString() const
{
    return strprintf("BlockRewards(nStakeReward=%s, nMasternodeReward=%s, nTreasuryReward=%s, nCharityReward=%s, nLotteryReward=%s, nProposalsReward=%s)",
                     FormatMoney(nStakeReward), FormatMoney(nMasternodeReward), FormatMoney(nTreasuryReward),
                     FormatMoney(nCharityReward), FormatMoney(nLotteryReward), FormatMoney(nProposalsReward));
}
