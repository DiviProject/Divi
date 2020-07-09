#ifndef BLOCK_INCENTIVES_POPULATOR_H
#define BLOCK_INCENTIVES_POPULATOR_H

#include <base58.h>

class CMutableTransaction;
class CBlockRewards;
class CBlockIndex;
class BlockIncentivesPopulator
{
private:
    static CBitcoinAddress TreasuryPaymentAddress();
    static CBitcoinAddress CharityPaymentAddress();

    static void FillTreasuryPayment(CMutableTransaction &tx, int nHeight);
    static void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex);
public:
    void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake);
};
#endif // BLOCK_INCENTIVES_POPULATOR_H