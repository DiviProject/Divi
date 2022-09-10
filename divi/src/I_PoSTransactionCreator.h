#ifndef I_POS_TRANSACTION_CREATOR_H
#define I_POS_TRANSACTION_CREATOR_H
#include <stdint.h>
class CMutableTransaction;
class CBlockIndex;
class CBlock;

class I_PoSTransactionCreator
{
public:
    virtual ~I_PoSTransactionCreator(){}
    virtual bool CreateProofOfStake(
        const CBlockIndex* chainTip,
        CBlock& block,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime) = 0;
};
#endif// I_POS_TRANSACTION_CREATOR_H