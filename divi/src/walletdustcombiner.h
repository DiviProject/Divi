#ifndef WALLET_DUST_COMBINER_H
#define WALLET_DUST_COMBINER_H
#include <amount.h>
class CWallet;
class Settings;
class WalletDustCombiner
{
private:
    CWallet& wallet_;
public:
    WalletDustCombiner(CWallet& wallet);
    void CombineDust(CAmount combineThreshold);
};
void combineWalletDust(const Settings& settings);
#endif // WALLET_DUST_COMBINER_H