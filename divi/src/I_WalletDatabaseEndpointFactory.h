#ifndef I_WALLET_DATABASE_ENDPOINT_FACTORY_H
#define I_WALLET_DATABASE_ENDPOINT_FACTORY_H
#include <I_WalletDatabase.h>
namespace boost
{
class thread_group;
}

class I_WalletDatabaseEndpointFactory
{
public:
    virtual ~I_WalletDatabaseEndpointFactory(){}
    virtual std::unique_ptr<I_WalletDatabase> getDatabaseEndpoint() const = 0;
    virtual void enableBackgroundDatabaseFlushing(boost::thread_group& threadGroup) const {}
};
#endif// I_WALLET_DATABASE_ENDPOINT_FACTORY_H