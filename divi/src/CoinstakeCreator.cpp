#include <CoinstakeCreator.h>

#include <wallet.h>

CoinstakeCreator::CoinstakeCreator(
    CWallet& wallet,
    int64_t& coinstakeSearchInterval
    ): wallet_(wallet)
    , coinstakeSearchInterval_(coinstakeSearchInterval)
{

}
bool CoinstakeCreator::CreateAndFindStake(
    uint32_t blockBits,
    int64_t nSearchTime, 
    int64_t& nLastCoinStakeSearchTime, 
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        if (wallet_.CreateCoinStake(wallet_, blockBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime)) {
            fStakeFound = true;
        }
        coinstakeSearchInterval_ = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}