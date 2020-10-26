#include "wallet.h"
#include <script/standard.h>

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include <boost/foreach.hpp>
#include <test/test_only.h>

#include <script/StakingVaultScript.h>
#include <random.h>

static const CHDChain& getHDWalletSeedForTesting()
{
    static CCriticalSection cs_testing;
    static bool toBeConstructed = true;
    static CHDChain hdchain;
    LOCK(cs_testing);

    if(toBeConstructed)
    {
        std::string walletName = "wallet_coinmanagement_tests.dat";
        std::unique_ptr<CWallet> walletPtr(new CWallet(walletName));
        bool firstLoad = true;
        CPubKey newDefaultKey;
        walletPtr->LoadWallet(firstLoad);
        walletPtr->GenerateNewHDChain();
        walletPtr->GetHDChain(hdchain);

        toBeConstructed = false;
    }
    return hdchain;
}


std::unique_ptr<CWallet> populateWalletWithKeys(std::string walletName)
{
    std::unique_ptr<CWallet> walletPtr(new CWallet(walletName));
    walletPtr->defaultKeyPoolTopUp = (int64_t)3;
    bool firstLoad = true;
    CPubKey newDefaultKey;
    walletPtr->LoadWallet(firstLoad);
    //walletPtr->GenerateNewHDChain();
    walletPtr->SetHDChain(getHDWalletSeedForTesting(),false);
    walletPtr->SetMinVersion(FEATURE_HD);
    walletPtr->GetKeyFromPool(newDefaultKey, false);
    walletPtr->SetDefaultKey(newDefaultKey);
    return std::move(walletPtr);
}

CMutableTransaction createDefaultTransaction(CScript defaultScript,unsigned& index, unsigned numberOfCoins)
{
    static int nextLockTime = 0;
    int numberOfIndices = GetRand(10)+1;
    index = GetRand(numberOfIndices);
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.vout.resize(numberOfIndices);
    tx.vout[index].nValue = numberOfCoins*COIN;
    tx.vout[index].scriptPubKey = defaultScript;
    tx.vin.resize(1);
    return tx;
}

static int walletCounter = 0;
class WalletCoinManagementTestFixture
{
public:
    const std::string walletName;
private:
    std::unique_ptr<CWallet> walletManager_;
public:
    CWallet& currentWallet;
    WalletCoinManagementTestFixture(
        ): walletName(std::string("currentWallet")+std::to_string(walletCounter++)+std::string(".dat") )
        , walletManager_( populateWalletWithKeys(walletName) )
        , currentWallet(*walletManager_)
    {

    }

    const CWalletTx& AddDefaultTxToWallet(CScript scriptToPayTo, unsigned& outputIndex,unsigned numberOfCoins = 100u)
    {
        CMutableTransaction tx = createDefaultTransaction(scriptToPayTo,outputIndex,numberOfCoins);
        CWalletTx wtx(&currentWallet, tx);
        currentWallet.AddToWallet(wtx);
        const CWalletTx* txPtr = currentWallet.GetWalletTx(wtx.GetHash());
        assert(txPtr);
        return *txPtr;
    }

    CScript vaultScriptAsOwner() const
    {
        CKey managerKey;
        managerKey.MakeNewKey(true);
        return CreateStakingVaultScript(
            ToByteVector(currentWallet.vchDefaultKey.GetID()),
            ToByteVector(managerKey.GetPubKey().GetID()));
    }
    CScript vaultScriptAsManager() const
    {
        CKey ownerKey;
        ownerKey.MakeNewKey(true);
        return CreateStakingVaultScript(
            ToByteVector(ownerKey.GetPubKey().GetID()),
            ToByteVector(currentWallet.vchDefaultKey.GetID()) );
    }

    CPubKey getNewKey()
    {
        CPubKey nextKeyGenerated;
        currentWallet.GetKeyFromPool(nextKeyGenerated, false);
        return nextKeyGenerated;
    }
};

BOOST_FIXTURE_TEST_SUITE(WalletCoinManagementTests,WalletCoinManagementTestFixture)

BOOST_AUTO_TEST_CASE(willAllowSpendingUnlockedCoin)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willNotAllowSpendingLockedCoin)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());

    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.LockCoin(COutPoint(wtx.GetHash(),outputIndex));

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(!fIsSpendable);/**/
}

BOOST_AUTO_TEST_CASE(willNotAllowSpendingFromKeyOutsideWallet)
{
    CKey key; key.MakeNewKey(true);
    CPubKey nonWalletPubKey = key.GetPubKey();
    CScript defaultScript = GetScriptForDestination(nonWalletPubKey.GetID());

     unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(!fIsSpendable);/**/
}
BOOST_AUTO_TEST_CASE(willNotAllowSpendingFromWatchOnlyAddress)
{
    CKey key; key.MakeNewKey(true);
    CPubKey nonWalletPubKey = key.GetPubKey();
    CScript defaultScript = GetScriptForDestination(nonWalletPubKey.GetID());


    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddWatchOnly(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(!fIsSpendable);/**/
}

BOOST_AUTO_TEST_CASE(willNotAllowSpendingFromWatchOnlyAddressEvenIfOwned)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddWatchOnly(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(!fIsSpendable);/**/
}

BOOST_AUTO_TEST_CASE(willAllowSpendingLockedCoinAfterUnlock)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    currentWallet.LockCoin(COutPoint(wtx.GetHash(),outputIndex));
    currentWallet.UnlockCoin(COutPoint(wtx.GetHash(),outputIndex));

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willMakeNoDistinctionBetweenAllCoinsAndStakableCoins)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(fIsSpendable);
    fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedAndSpendableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfOwnerAndSpendableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfOwnedAndStakableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
}
BOOST_AUTO_TEST_CASE(willAllowSelectingVaultFundsIfManagedAndStakableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
}
BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedButScriptNotAdded)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedAndOwnedVaultCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willAllowSelectingVaultFundsIfOwnerAndOwnedVaultCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingNonVaultFundsIfOwnedVaultCoinsRequested)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
    BOOST_CHECK(!fIsSpendable);
    fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,wtx.GetHash(),outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_SUITE_END()
