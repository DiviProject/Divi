#ifndef BLOCK_INCENTIVES_POPULATOR_H
#define BLOCK_INCENTIVES_POPULATOR_H

#include <string>

class CMutableTransaction;
class CBlockRewards;
class CBlockIndex;
class CChainParams;
class CChain;
class CMasternodePayments;
class I_SuperblockHeightValidator;
class I_BlockSubsidyProvider;
class BlockIncentivesPopulator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    CMasternodePayments& masternodePayments_;
    const I_SuperblockHeightValidator& heightValidator_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const std::string treasuryPaymentAddress_;
    const std::string charityPaymentAddress_;

private:
    void FillTreasuryPayment(CMutableTransaction &tx, int nHeight);
    void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex);

public:
    BlockIncentivesPopulator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        CMasternodePayments& masternodePayments,
        const I_SuperblockHeightValidator& heightValidator,
        const I_BlockSubsidyProvider& blockSubsidies);
    void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake);
};
#endif // BLOCK_INCENTIVES_POPULATOR_H