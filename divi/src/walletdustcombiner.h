#ifndef WALLET_DUST_COMBINER_H
#define WALLET_DUST_COMBINER_H
#include <amount.h>
class CWallet;
class WalletDustCombiner
{
private:
    CWallet& wallet_;
public:
    WalletDustCombiner(CWallet& wallet);
    void CombineDust(CAmount combineThreshold);
};

#endif // WALLET_DUST_COMBINER_H