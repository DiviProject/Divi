#ifndef MULTI_WALLET_MODULE_H
#define MULTI_WALLET_MODULE_H
#include <vector>
#include <memory>
#include <string>
#include <map>

#include <LoadWalletResult.h>
#include <ChainstateManager.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

class CWallet;
class CTxMemPool;
class I_WalletDatabaseEndpointFactory;
class LegacyWalletDatabaseEndpointFactory;
class Settings;
class CCriticalSection;

namespace boost
{
class thread_group;
} // namespace boost

class MultiWalletModule
{
private:
    ChainstateManager::Reference chainStateReference_;
    Settings& settings_;
    const bool walletIsDisabled_;
    std::string walletFilename_;
    std::unique_ptr<I_MerkleTxConfirmationNumberCalculator> confirmationCalculator_;
    std::unique_ptr<LegacyWalletDatabaseEndpointFactory> walletDbEndpointFactory_;
    std::unique_ptr<CWallet> activeWallet_;
public:
    MultiWalletModule(
        Settings& settings,
        CTxMemPool& transactionMemoryPool,
        CCriticalSection& mmainCriticalSection,
        const int coinbaseConfirmationsForMaturity);
    ~MultiWalletModule();

    const I_MerkleTxConfirmationNumberCalculator& getConfirmationsCalculator() const;
    const LegacyWalletDatabaseEndpointFactory& getWalletDbEnpointFactory() const;
    CWallet* getActiveWallet() const;
};
#endif// MULTI_WALLET_MODULE_H