#include <LegacyBlockSubsidies.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <spork.h>
#include <timedata.h>
#include <chain.h>

namespace Legacy
{

namespace
{

CAmount BlockSubsidy(int nHeight, const CChainParams& chainParameters)
{
    if(nHeight == 0) {
        return 50 * COIN;
    } else if (nHeight == 1) {
        return chainParameters.premineAmt;
    }

    CAmount nSubsidy = std::max(
        1250 - 100* std::max(nHeight/chainParameters.SubsidyHalvingInterval() -1,0),
        250)*COIN;

    return nSubsidy;
}

CAmount GetFullBlockValue(int nHeight, const CChainParams& chainParameters)
{
    CAmount blockSubsidy = 0u;
    if(GetSporkManager().GetFullBlockValue(nHeight,chainParameters,blockSubsidy))
    {
        return blockSubsidy;
    }
    else
    {
        return BlockSubsidy(nHeight, chainParameters);
    }
}

} // anonymous namespace

CBlockRewards GetBlockSubsidity(int nHeight, const CChainParams& chainParameters)
{
    CAmount nSubsidy = GetFullBlockValue(nHeight,chainParameters);

    if(nHeight <= chainParameters.LAST_POW_BLOCK()) {
        return CBlockRewards(nSubsidy, 0, 0, 0, 0, 0);
    }

    CAmount nLotteryPart = (nHeight >= chainParameters.GetLotteryBlockStartBlock()) ? (50 * COIN) : 0;

    assert(nSubsidy >= nLotteryPart);
    nSubsidy -= nLotteryPart;

    auto helper = [nHeight,&chainParameters,nSubsidy,nLotteryPart](
        int nStakePercentage,
        int nMasternodePercentage,
        int nTreasuryPercentage,
        int nProposalsPercentage,
        int nCharityPercentage)
    {
        auto helper = [nSubsidy](int percentage) {
            return (nSubsidy * percentage) / 100;
        };

        return CBlockRewards(
            helper(nStakePercentage),
            helper(nMasternodePercentage),
            helper(nTreasuryPercentage),
            helper(nCharityPercentage),
            nLotteryPart,
            helper(nProposalsPercentage));
    };

    BlockPaymentSporkValue rewardDistribution;
    if(GetSporkManager().GetRewardDistribution(nHeight,chainParameters,rewardDistribution))
    {
        return helper(
                rewardDistribution.nStakeReward,
                rewardDistribution.nMasternodeReward,
                rewardDistribution.nTreasuryReward,
                rewardDistribution.nProposalsReward,
                rewardDistribution.nCharityReward);
    }
    else
    {
        return helper(38, 45, 16, 0, 1);
    }
}


bool IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetLotteryBlockStartBlock() &&
            ((nBlockHeight % chainParams.GetLotteryBlockCycle()) == 0);
}

bool IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % chainParams.GetTreasuryPaymentsCycle()) == 0);
}

int64_t GetTreasuryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nTreasuryReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t GetCharityReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nCharityReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t GetLotteryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    // 50 coins every block for lottery
    return rewards.nLotteryReward*chainParameters.GetLotteryBlockCycle();
}

} // namespace Legacy
