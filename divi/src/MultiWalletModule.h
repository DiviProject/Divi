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
class CChain;
class BlockMap;

namespace boost
{
class thread_group;
} // namespace boost

class DatabaseBackedWallet
{
private:
    DatabaseBackedWallet& operator=(const DatabaseBackedWallet& other) = delete;
public:
    std::string walletFilename_;
    std::unique_ptr<CCriticalSection> underlyingWalletCriticalSection_;
    std::unique_ptr<LegacyWalletDatabaseEndpointFactory> walletDbEndpointFactory_;
    std::unique_ptr<CWallet> wallet_;
    DatabaseBackedWallet(
        const std::string walletFilename,
        Settings& settings,
        const CChain& activeChain,
        const BlockMap& blockIndicesByHash,
        const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
        CCriticalSection* underlyingWalletCriticalSection);
    ~DatabaseBackedWallet();
    DatabaseBackedWallet(DatabaseBackedWallet&& other);
};
class MultiWalletModule
{
private:
    ChainstateManager::Reference chainStateReference_;
    Settings& settings_;
    const bool walletIsDisabled_;
    std::unique_ptr<I_MerkleTxConfirmationNumberCalculator> confirmationCalculator_;
    std::map<std::string, std::unique_ptr<DatabaseBackedWallet>> backedWalletsByName_;
    DatabaseBackedWallet* activeWallet_;
public:
    MultiWalletModule(
        Settings& settings,
        CTxMemPool& transactionMemoryPool,
        CCriticalSection& mmainCriticalSection,
        const int coinbaseConfirmationsForMaturity);
    ~MultiWalletModule();

    bool reloadActiveWallet();
    bool loadWallet(const std::string walletFilename);
    bool setActiveWallet(const std::string walletFilename);
    const I_MerkleTxConfirmationNumberCalculator& getConfirmationsCalculator() const;
    const LegacyWalletDatabaseEndpointFactory& getWalletDbEnpointFactory() const;
    CWallet* getActiveWallet() const;
};
#endif// MULTI_WALLET_MODULE_H