#include <LegacyBlockSubsidies.h>

#include <chainparams.h>
#include <BlockRewards.h>
#include <spork.h>
#include <timedata.h>
#include <chain.h>

extern CChain chainActive;

// Legacy methods
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
CAmount Legacy::GetFullBlockValue(int nHeight, const CChainParams& chainParameters)
{
    if(sporkManager.IsSporkActive(SPORK_15_BLOCK_VALUE)) {
        MultiValueSporkList<BlockSubsiditySporkValue> vBlockSubsiditySporkValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager.GetMultiValueSpork(SPORK_15_BLOCK_VALUE), vBlockSubsiditySporkValues);
        auto nBlockTime = chainActive[nHeight] ? chainActive[nHeight]->nTime : GetAdjustedTime();
        BlockSubsiditySporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vBlockSubsiditySporkValues, nHeight, nBlockTime);

        if(activeSpork.IsValid() && 
            (activeSpork.nActivationBlockHeight % chainParameters.SubsidyHalvingInterval()) == 0 )
        {
            // we expect that this value is in coins, not in satoshis
            return activeSpork.nBlockSubsidity * COIN;
        }
    }

    return BlockSubsidy(nHeight, chainParameters);
}

CBlockRewards Legacy::GetBlockSubsidity(int nHeight, const CChainParams& chainParameters)
{
    CAmount nSubsidy = Legacy::GetFullBlockValue(nHeight,chainParameters);

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

    if(sporkManager.IsSporkActive(SPORK_13_BLOCK_PAYMENTS)) {
        MultiValueSporkList<BlockPaymentSporkValue> vBlockPaymentsValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager.GetMultiValueSpork(SPORK_13_BLOCK_PAYMENTS), vBlockPaymentsValues);
        auto nBlockTime = chainActive[nHeight] ? chainActive[nHeight]->nTime : GetAdjustedTime();
        BlockPaymentSporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vBlockPaymentsValues, nHeight, nBlockTime);

        if(activeSpork.IsValid() &&
            (activeSpork.nActivationBlockHeight % chainParameters.SubsidyHalvingInterval()) == 0 ) {
            // we expect that this value is in coins, not in satoshis
            return helper(
                activeSpork.nStakeReward,
                activeSpork.nMasternodeReward,
                activeSpork.nTreasuryReward, 
                activeSpork.nProposalsReward, 
                activeSpork.nCharityReward);
        }
    }


    return helper(38, 45, 16, 0, 1);
}


bool Legacy::IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetLotteryBlockStartBlock() &&
            ((nBlockHeight % chainParams.GetLotteryBlockCycle()) == 0);
}

bool Legacy::IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams)
{
    return nBlockHeight >= chainParams.GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % chainParams.GetTreasuryPaymentsCycle()) == 0);
}

int64_t Legacy::GetTreasuryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nTreasuryReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t Legacy::GetCharityReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    return rewards.nCharityReward*chainParameters.GetTreasuryPaymentsCycle();
}

int64_t Legacy::GetLotteryReward(const CBlockRewards &rewards, const CChainParams& chainParameters)
{
    // 50 coins every block for lottery
    return rewards.nLotteryReward*chainParameters.GetLotteryBlockCycle();
}