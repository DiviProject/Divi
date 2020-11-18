#ifndef I_POS_TRANSACTION_CREATOR_H
#define I_POS_TRANSACTION_CREATOR_H
#include <stdint.h>
class CMutableTransaction;
class I_PoSTransactionCreator
{
public:
    virtual ~I_PoSTransactionCreator(){}
    virtual bool CreateProofOfStake(
        uint32_t blockBits,
        int64_t nSearchTime,
        int64_t& nLastCoinStakeSearchTime,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime) = 0;
};
#endif// I_POS_TRANSACTION_CREATOR_H