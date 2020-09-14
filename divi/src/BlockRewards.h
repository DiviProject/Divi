#ifndef BLOCK_REWARDS_H
#define BLOCK_REWARDS_H
#include "amount.h"
#include <string>

struct CBlockRewards {
    CBlockRewards(
        CAmount nStakeReward,
        CAmount nMasternodeReward,
        CAmount nTreasuryReward,
        CAmount nCharityReward, 
        CAmount nLotteryReward, 
        CAmount nProposalsReward);

    std::string ToString() const;

    CAmount nStakeReward;
    CAmount nMasternodeReward;
    CAmount nTreasuryReward;
    CAmount nCharityReward;
    CAmount nLotteryReward;
    CAmount nProposalsReward;

    CAmount total() const;
};
#endif //BLOCK_REWARDS_H
