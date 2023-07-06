#ifndef LEGACY_WALLET_DATABASE_ENDPOINT_FACTORY_H
#define LEGACY_WALLET_DATABASE_ENDPOINT_FACTORY_H
#include <I_WalletDatabaseEndpointFactory.h>
#include <string>
class Settings;
class LegacyWalletDatabaseEndpointFactory final: public I_WalletDatabaseEndpointFactory
{
private:
    const std::string walletFilename_;
    Settings& settings_;
public:
    LegacyWalletDatabaseEndpointFactory(const std::string walletFilename, Settings& settings);
    std::unique_ptr<I_WalletDatabase> getDatabaseEndpoint() const override;

    void enableBackgroundDatabaseFlushing(boost::thread_group& threadGroup) const;
    void enableBackgroundMonthlyWalletBackup(boost::thread_group& threadGroup,const std::string dataDirectory, bool regtestMode) const;
    bool backupWalletFile(const std::string& strDest) const;
};
#endif// LEGACY_WALLET_DATABASE_ENDPOINT_FACTORY_H