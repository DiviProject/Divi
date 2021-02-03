#ifndef BLOCK_INCENTIVES_POPULATOR_H
#define BLOCK_INCENTIVES_POPULATOR_H
#include "I_BlockIncentivesPopulator.h"
#include <string>
#include <amount.h>

class CMutableTransaction;
class CBlockRewards;
class CBlockIndex;
class CChainParams;
class CChain;
class CMasternodePayments;
class I_SuperblockHeightValidator;
class I_BlockSubsidyProvider;
class CTransaction;
class CMasternodeSync;
class CSporkManager;

class BlockIncentivesPopulator : public I_BlockIncentivesPopulator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    const CMasternodeSync& masternodeSync_;
    CMasternodePayments& masternodePayments_;
    const I_SuperblockHeightValidator& heightValidator_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const CSporkManager& sporkManager_;
    const std::string treasuryPaymentAddress_;
    const std::string charityPaymentAddress_;

private:
    void FillTreasuryPayment(CMutableTransaction &tx, int nHeight) const;
    void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex) const;
    bool HasValidMasternodePayee(const CTransaction &txNew, const CBlockIndex* pindex) const;

public:
    BlockIncentivesPopulator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        const CMasternodeSync& masternodeSynchronization,
        CMasternodePayments& masternodePayments,
        const I_SuperblockHeightValidator& heightValidator,
        const I_BlockSubsidyProvider& blockSubsidies,
        const CSporkManager& sporkManager);

    void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, const CBlockIndex* chainTip, bool fProofOfStake) const override;
    bool IsBlockValueValid(const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight) const override;
    bool HasValidPayees(const CTransaction &txNew, const CBlockIndex* pindex) const override;
};

#endif // BLOCK_INCENTIVES_POPULATOR_H
