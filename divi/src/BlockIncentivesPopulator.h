#ifndef BLOCK_INCENTIVES_POPULATOR_H
#define BLOCK_INCENTIVES_POPULATOR_H

#include <base58.h>

class CMutableTransaction;
class CBlockRewards;
class CBlockIndex;
class CChainParams;
class CChain;
class CMasternodePayments;
class BlockIncentivesPopulator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    CMasternodePayments& masternodePayments_;
private:
    CBitcoinAddress TreasuryPaymentAddress();
    CBitcoinAddress CharityPaymentAddress();

    void FillTreasuryPayment(CMutableTransaction &tx, int nHeight);
    void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex);
public:
    BlockIncentivesPopulator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        CMasternodePayments& masternodePayments);
    void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake);
};
#endif // BLOCK_INCENTIVES_POPULATOR_H