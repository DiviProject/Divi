#ifndef  LEGACY_BLOCK_SUBSIDIES_H
#define  LEGACY_BLOCK_SUBSIDIES_H
#include <amount.h>
class CBlockRewards;
class CChainParams;
class CSporkManager;

namespace Legacy
{
    bool IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    bool IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    int64_t GetTreasuryReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    int64_t GetCharityReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    int64_t GetLotteryReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    CBlockRewards GetBlockSubsidity(int nHeight, const CChainParams& chainParams, const CSporkManager& sporkManager);
}
#endif // LEGACY_BLOCK_SUBSIDIES_H
