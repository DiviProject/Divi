#include <test_only.h>

#include <walletdb.h>
#include <Settings.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
#include <MultiWalletModule.h>
#include <txmempool.h>

BOOST_AUTO_TEST_CASE(willVerifyTwoDatabasesCanBeOpenAtTheSameTime)
{
    try
    {
        Settings& localSettings = Settings::instance();
        {
            CWalletDB firstWallet(localSettings, "myfirstwallet.dat","cr+");
            CWalletDB secondWallet(localSettings, "mysecondwallet.dat","cr+");
        }
        CWalletDB firstWalletReopen(localSettings, "myfirstwallet.dat","r+");
        CWalletDB secondWalletReopen(localSettings, "mysecondwallet.dat","r+");
    }
    catch(const std::exception& e)
    {
        BOOST_CHECK_EQUAL_MESSAGE(false,true,e.what());
    }
}

BOOST_AUTO_TEST_CASE(willVerifyTwoLegacyDatabaseEnpointFactoriesCanBeOpenAtTheSameTime)
{
    try
    {
        Settings& localSettings = Settings::instance();
        LegacyWalletDatabaseEndpointFactory endpoint1("myfirstwallet_b.dat",localSettings);
        LegacyWalletDatabaseEndpointFactory endpoint2("mysecondwallet_b.dat",localSettings);
    }
    catch(const std::exception& e)
    {
        BOOST_CHECK_EQUAL_MESSAGE(false,true,e.what());
    }
}

BOOST_AUTO_TEST_CASE(willVerifyMultiWalletModuleCanLoadTwoWallets)
{
    try
    {
        Settings& localSettings = Settings::instance();
        CTxMemPool mempool;
        CCriticalSection mainCriticalSection;
        MultiWalletModule module(localSettings,mempool,mainCriticalSection,20);

        module.loadWallet("myfirstwallet_c.dat");
        module.loadWallet("mysecondtwallet_c.dat");
    }
    catch(const std::exception& e)
    {
        BOOST_CHECK_EQUAL_MESSAGE(false,true,e.what());
    }
}