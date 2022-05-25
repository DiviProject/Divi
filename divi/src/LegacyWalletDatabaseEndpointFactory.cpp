#include <LegacyWalletDatabaseEndpointFactory.h>

#include <walletdb.h>
#include <Settings.h>
#include <utiltime.h>

#include <ThreadManagementHelpers.h>
#include <boost/thread.hpp>

namespace
{

void ThreadFlushWalletDB(Settings& settings, const std::string& strFile)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("divi-wallet");

    static bool fOneThread;
    if (fOneThread)
        return;
    fOneThread = true;

    CWalletDB walletDbToFlush(settings,strFile,"flush");
    const unsigned& walletDbUpdated = walletDbToFlush.numberOfWalletUpdates();
    unsigned int nLastSeen =  walletDbUpdated;
    unsigned int nLastFlushed = walletDbUpdated;
    int64_t nLastWalletUpdate = GetTime();
    while (true) {
        MilliSleep(500);

        if (nLastSeen != walletDbUpdated) {
            nLastSeen = walletDbUpdated;
            nLastWalletUpdate = GetTime();
        }

        if (nLastFlushed != walletDbUpdated && GetTime() - nLastWalletUpdate >= 2) {
            if (walletDbToFlush.Flush())
            {
                nLastFlushed = walletDbUpdated;
            }
        }
    }
}

} // namespace name

LegacyWalletDatabaseEndpointFactory::LegacyWalletDatabaseEndpointFactory(
    const std::string walletFilename,
    Settings& settings
    ): walletFilename_(walletFilename)
    , settings_(settings)
{
    CWalletDB(settings_,walletFilename_,"cr+");
}

std::unique_ptr<I_WalletDatabase> LegacyWalletDatabaseEndpointFactory::getDatabaseEndpoint() const
{
    return std::unique_ptr<I_WalletDatabase>{new CWalletDB(settings_,walletFilename_)};
}

void LegacyWalletDatabaseEndpointFactory::enableBackgroundDatabaseFlushing(boost::thread_group& threadGroup) const
{
    threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, settings_, walletFilename_ ));
}