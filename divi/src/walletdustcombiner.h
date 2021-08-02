#ifndef WALLET_DUST_COMBINER_H
#define WALLET_DUST_COMBINER_H
#include <amount.h>
class CWallet;
class Settings;
class CFeeRate;
class WalletDustCombiner
{
private:
    CWallet& wallet_;
    const CFeeRate& relayFeeRate_;
    const int coinbaseMaturity_;
public:
    WalletDustCombiner(
        CWallet& wallet,
        const CFeeRate& relayFeeRate,
        const int coinbaseMaturity);
    void CombineDust(CAmount combineThreshold);
};
void combineWalletDust(const Settings& settings);
#endif // WALLET_DUST_COMBINER_H