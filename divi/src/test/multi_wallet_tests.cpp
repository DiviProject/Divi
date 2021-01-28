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
        const std::string walletName = walletCache_[walletID]->strWalletFile;
        walletCache_[walletID].reset();
        return walletName;
    }

    const CWalletTx& AddDefaultTxToWallet(FakeWallet& currentWallet, const CAmount amount)
    {
        assert(currentWallet.vchDefaultKey.IsValid());
        CScript scriptToPayTo = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
        unsigned outputIndex;
        return currentWallet.AddDefaultTx(scriptToPayTo, outputIndex, amount);
    }

    FakeWallet& getWallet(const unsigned walletID) const
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
            auto& wallet = getWallet(walletID);
            const CWalletTx& tx = AddDefaultTxToWallet(wallet, 100 * COIN);
            wallet.FakeAddToChain(tx);
        }
        BOOST_CHECK_EQUAL_MESSAGE(
            getWallet(walletID).GetBalance(),
            CAmount(totalTxsPerWallet*100*COIN),
            "Balance is different from expected!");
    }
}

BOOST_AUTO_TEST_SUITE_END()
