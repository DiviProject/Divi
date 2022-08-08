#include <wallet.h>
#include <WalletTx.h>
#include <script/standard.h>

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include <boost/foreach.hpp>
#include <test/test_only.h>

#include <random.h>
#include <chain.h>
#include <blockmap.h>
#include <test/FakeBlockIndexChain.h>
#include <test/FakeWallet.h>

class MultiWalletTestFixture
{
private:
    std::vector<std::unique_ptr<FakeWallet>> walletCache_;
    FakeBlockIndexWithHashes fakeChain_;
public:
    MultiWalletTestFixture(
        ): walletCache_()
        , fakeChain_(1, 1600000000, 1)
    {}

    void createWallets(const unsigned walletCount)
    {
        if(!walletCache_.empty()) return;

        walletCache_.resize(walletCount);
        for(unsigned walletID = 0; walletID < walletCount; ++ walletID)
            walletCache_[walletID].reset(new FakeWallet(fakeChain_));
    }
    std::string deallocateWalletAndGetName(const unsigned walletID)
    {
        if(walletCache_.empty() || walletID >= walletCache_.size() || !walletCache_[walletID].get()) return "";
        std::unique_ptr<FakeWallet>& fakeWalletPtr = walletCache_[walletID];
        const std::string walletName = fakeWalletPtr->dbFilename();
        fakeWalletPtr.reset();
        return walletName;
    }
    void deallocateAndReloadWallet(const unsigned walletID)
    {
        if(walletCache_.empty() || walletID >= walletCache_.size() || !walletCache_[walletID].get()) return;
        std::unique_ptr<FakeWallet>& fakeWalletPtr = walletCache_[walletID];
        const std::string walletName = fakeWalletPtr->dbFilename();
        fakeWalletPtr.reset();
        fakeWalletPtr.reset(new FakeWallet(fakeChain_,walletName));
    }

    const CWalletTx& AddDefaultTxToWallet(FakeWallet& currentWallet, const CAmount amount)
    {
        CWallet& wrappedWallet = currentWallet.getWallet();
        CPubKey destinationPubKey;
        assert(wrappedWallet.GetKeyFromPool(destinationPubKey,true));
        CScript scriptToPayTo = GetScriptForDestination(destinationPubKey.GetID());
        unsigned outputIndex;
        return currentWallet.AddDefaultTx(scriptToPayTo, outputIndex, amount);
    }

    FakeWallet& getFakeWallet(const unsigned walletID) const
    {
        if(walletID < walletCache_.size())
        {
            return *walletCache_[walletID];
        }
        assert(false);
    }
};

BOOST_FIXTURE_TEST_SUITE(AdditionalWalletCoexistenceTests,MultiWalletTestFixture)

BOOST_AUTO_TEST_CASE(multipleWalletsCanCoexist)
{
    const unsigned totalWalletCount = 13;
    const unsigned totalTxsPerWallet = 23;
    createWallets(totalWalletCount);
    for(unsigned walletID =0 ; walletID < totalWalletCount; ++walletID)
    {
        for(unsigned txCount = 0; txCount < totalTxsPerWallet; ++txCount)
        {
            auto& fakeWallet = getFakeWallet(walletID);
            const CWalletTx& tx = AddDefaultTxToWallet(fakeWallet, 100 * COIN);
            fakeWallet.FakeAddToChain(tx);
        }
        CWallet& wrappedWallet = getFakeWallet(walletID).getWallet();
        BOOST_CHECK_EQUAL_MESSAGE(
            wrappedWallet.GetBalance(),
            CAmount(totalTxsPerWallet*100*COIN),
            "Balance is different from expected!");
    }
}

BOOST_AUTO_TEST_CASE(multipleWalletsCanCoexistAndBeIndependentlyReloaded)
{
    const unsigned totalWalletCount = 13;
    const unsigned totalTxsPerWallet = 23;
    createWallets(totalWalletCount);
    for(unsigned walletID =0 ; walletID < totalWalletCount; ++walletID)
    {
        for(unsigned txCount = 0; txCount < totalTxsPerWallet; ++txCount)
        {
            auto& fakeWallet = getFakeWallet(walletID);
            const CWalletTx& tx = AddDefaultTxToWallet(fakeWallet, 100 * COIN);
            fakeWallet.FakeAddToChain(tx);
        }
        CWallet& wrappedWallet = getFakeWallet(walletID).getWallet();
        BOOST_CHECK_EQUAL_MESSAGE(
            wrappedWallet.GetBalance(),
            CAmount(totalTxsPerWallet*100*COIN),
            "Balance is different from expected!");

        if(walletID > 5)
        {
            const unsigned walletToDeallocate = walletID - 4;
            deallocateAndReloadWallet(walletToDeallocate);
            auto& fakeWallet = getFakeWallet(walletToDeallocate);
            fakeWallet.SetConfirmedTxsToVerified();
            CWallet& wrappedWallet = fakeWallet.getWallet();
            BOOST_CHECK_EQUAL_MESSAGE(
                wrappedWallet.GetBalance(),
                CAmount(totalTxsPerWallet*100*COIN),
                "Balance is different from expected!");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
