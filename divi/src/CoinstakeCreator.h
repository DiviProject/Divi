#ifndef COINSTAKE_CREATOR_H
#define COINSTAKE_CREATOR_H

#include <stdint.h>
class CWallet;
class CBlock;
class CMutableTransaction;

class CoinstakeCreator
{
private:
    CWallet& wallet_;
    int64_t& coinstakeSearchInterval_;
public:
    CoinstakeCreator(
        CWallet& wallet,
        int64_t& coinstakeSearchInterval);
    bool CreateAndFindStake(
        uint32_t blockBits,
        int64_t nSearchTime, 
        int64_t& nLastCoinStakeSearchTime, 
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime);
};
#endif // COINSTAKE_CREATOR_H