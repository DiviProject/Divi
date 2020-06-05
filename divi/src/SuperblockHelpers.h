#ifndef SUPERBLOCK_HELPERS_H
#define SUPERBLOCK_HELPERS_H
#include <stdint.h>
#include <amount.h>
#include <I_SuperblockHeightValidator.h>
#include <I_BlockSubsidyProvider.h>

class CBlockRewards;
class CChainParams;

namespace Legacy
{
    bool IsValidLotteryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    bool IsValidTreasuryBlockHeight(int nBlockHeight,const CChainParams& chainParams);
    int64_t GetTreasuryReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    int64_t GetCharityReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    int64_t GetLotteryReward(const CBlockRewards &rewards, const CChainParams& chainParams);
    CBlockRewards GetBlockSubsidity(int nHeight, const CChainParams& chainParams);
    CAmount GetFullBlockValue(int nHeight, const CChainParams& chainParams);
};

bool IsValidLotteryBlockHeight(int nBlockHeight);
bool IsValidTreasuryBlockHeight(int nBlockHeight);

int64_t GetTreasuryReward(const CBlockRewards &rewards);
int64_t GetCharityReward(const CBlockRewards &rewards);
int64_t GetLotteryReward(const CBlockRewards &rewards);

CBlockRewards GetBlockSubsidity(int nHeight);

class SuperblockHeightValidator: public I_SuperblockHeightValidator
{
private:
    const CChainParams& chainParameters_;
    int transitionHeight_;
    int superblockCycleLength_;

public:
    SuperblockHeightValidator(const CChainParams& chainParameters);
    int getTransitionHeight() const;
    const CChainParams& getChainParameters() const;

    virtual int GetTreasuryBlockPaymentCycle(int nBlockHeight) const;
    virtual int GetLotteryBlockPaymentCycle(int nBlockHeight) const;
    virtual bool IsValidLotteryBlockHeight(int nBlockHeight) const;
    virtual bool IsValidTreasuryBlockHeight(int nBlockHeight) const;
};
class BlockSubsidyProvider: public I_BlockSubsidyProvider
{
private:
    const CChainParams& chainParameters_;
    I_SuperblockHeightValidator& heightValidator_;

    void updateTreasuryReward(int nHeight, CBlockRewards& rewards, bool isTreasuryBlock) const;
    void updateLotteryReward(int nHeight, CBlockRewards& rewards,bool isLotteryBlock) const;
public:
    BlockSubsidyProvider(
        const CChainParams& chainParameters,
        I_SuperblockHeightValidator& heightValidator);
    virtual CBlockRewards GetBlockSubsidity(int nHeight) const;
    virtual CAmount GetFullBlockValue(int nHeight) const;
};

class SuperblockSubsidyProvider
{
private:
    const CChainParams& chainParameters_;
    I_SuperblockHeightValidator& heightValidator_;
    I_BlockSubsidyProvider& blockSubsidyProvider_;
public:
    SuperblockSubsidyProvider(
        const CChainParams& chainParameters, 
        I_SuperblockHeightValidator& heightValidator,
        I_BlockSubsidyProvider& blockSubsidyProvider);
    
    CAmount GetTreasuryReward(int blockHeight) const;
    CAmount GetCharityReward(int blockHeight) const;
    CAmount GetLotteryReward(int blockHeight) const;
};
#endif // SUPERBLOCK_HELPERS_H
