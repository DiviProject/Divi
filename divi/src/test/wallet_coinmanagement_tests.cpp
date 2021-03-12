#include <wallet.h>
#include <WalletTx.h>
#include <script/standard.h>

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include <boost/foreach.hpp>
#include <test/test_only.h>

#include <script/StakingVaultScript.h>
#include <random.h>
#include <chain.h>
#include <blockmap.h>
#include <test/FakeBlockIndexChain.h>

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
        walletPtr->defaultKeyPoolTopUp = (int64_t)1;
        bool firstLoad = true;
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

CMutableTransaction createDefaultTransaction(CScript defaultScript,unsigned& index, CAmount amount)
{
    static int nextLockTime = 0;
    int numberOfIndices = GetRand(10)+1;
    index = GetRand(numberOfIndices);
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.vout.resize(numberOfIndices);
    for(CTxOut& output: tx.vout)
    {
        output.nValue = 1;
        output.scriptPubKey = CScript() << OP_TRUE;
    }
    tx.vout[index].nValue = amount;
    tx.vout[index].scriptPubKey = defaultScript;
    tx.vin.resize(1);
    {// Avoid flagging as a coinbase tx
        tx.vin[0].prevout = COutPoint(GetRandHash(),static_cast<uint32_t>(GetRand(100)) );
    }
    return tx;
}

static int walletCounter = 0;
class WalletCoinManagementTestFixture
{
public:
    const std::string walletName;
private:
    std::unique_ptr<CWallet> walletManager_;
    const int32_t version;
    int32_t timestamp;
    CChain activeChain_;
    BlockMap blockIndices_;
    FakeBlockIndexChain fakeChain_;
public:
    CWallet& currentWallet;

    uint256 extendFakeChainAndGetTipBlockHash()
    {
        uint256 hash;
        fakeChain_.extendBy(1,timestamp++,version);
        CBlockIndex* newTip = const_cast<CBlockIndex*>(fakeChain_.Tip());
        hash = GetRandHash();
        activeChain_.SetTip(newTip);
        blockIndices_.insert(std::make_pair(hash, newTip));
        return hash;
    }
    WalletCoinManagementTestFixture(
        ): walletName(std::string("currentWallet")+std::to_string(walletCounter++)+std::string(".dat") )
        , walletManager_( populateWalletWithKeys(walletName) )
        , version(0x01)
        , timestamp(1600000000)
        , activeChain_()
        , blockIndices_()
        , fakeChain_()
        , currentWallet(*walletManager_)
    {
        extendFakeChainAndGetTipBlockHash(); // Fake genesis block
    }

    const CWalletTx& AddDefaultTxToWallet(CScript scriptToPayTo, unsigned& outputIndex,CAmount amount = 100*COIN)
    {
        CMutableTransaction tx = createDefaultTransaction(scriptToPayTo,outputIndex,amount);
        CMerkleTx merkleTx(tx,activeChain_,blockIndices_);
        CWalletTx wtx(merkleTx);
        currentWallet.AddToWallet(wtx);
        const CWalletTx* txPtr = currentWallet.GetWalletTx(wtx.GetHash());
        assert(txPtr);
        return *txPtr;
    }
    void FakeAddTransactionToChain(const uint256& txHash)
    {
        CWalletTx* txPtr = const_cast<CWalletTx*>( currentWallet.GetWalletTx(txHash) );
        txPtr->hashBlock = extendFakeChainAndGetTipBlockHash();
        txPtr->nIndex = 0;
        txPtr->fMerkleVerified = true;
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
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willNotAllowSpendingLockedCoin)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());

    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.LockCoin(COutPoint(wtx.GetHash(),outputIndex));

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
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
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
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
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(!fIsSpendable);/**/
}

BOOST_AUTO_TEST_CASE(willNotAllowSpendingFromWatchOnlyAddressEvenIfOwned)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddWatchOnly(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
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
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willMakeNoDistinctionBetweenAllCoinsAndStakableCoins)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(fIsSpendable);
    fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedAndSpendableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfOwnerAndSpendableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfOwnedAndStakableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
}
BOOST_AUTO_TEST_CASE(willAllowSelectingVaultFundsIfManagedAndStakableCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
}
BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedButScriptNotAdded)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,STAKABLE_COINS));
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingVaultFundsIfManagedAndOwnedVaultCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsManager();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);
    currentWallet.AddCScript(defaultScript);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willAllowSelectingVaultFundsIfOwnerAndOwnedVaultCoinsSelected)
{
    CScript defaultScript =  vaultScriptAsOwner();
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
}

BOOST_AUTO_TEST_CASE(willDisallowSelectingNonVaultFundsIfOwnedVaultCoinsRequested)
{
    CScript defaultScript = GetScriptForDestination(getNewKey().GetID());
    unsigned outputIndex=0;
    const CWalletTx& wtx = AddDefaultTxToWallet(defaultScript,outputIndex);

    bool fIsSpendable = false;
    BOOST_CHECK(!currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,OWNED_VAULT_COINS));
    BOOST_CHECK(!fIsSpendable);
    fIsSpendable = false;
    BOOST_CHECK(currentWallet.IsAvailableForSpending(&wtx,outputIndex,nullptr,false,fIsSpendable,ALL_SPENDABLE_COINS));
    BOOST_CHECK(fIsSpendable);
}

BOOST_AUTO_TEST_CASE(willFindThatTransactionsByDefaultHaveNegativeDepth)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    unsigned outputIndex=0;
    auto normalTx = AddDefaultTxToWallet(normalScript,outputIndex,100*COIN);
    BOOST_CHECK_MESSAGE(normalTx.GetNumberOfBlockConfirmations()==-1,"Found wallet transaction has non-negative depth in empty chain!");
}

BOOST_AUTO_TEST_CASE(willFindThatTransactionsWillHaveDepthAccordingToLengthOfChain)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    unsigned outputIndex=0;
    const CWalletTx& normalTx = AddDefaultTxToWallet(normalScript,outputIndex,100*COIN);
    FakeAddTransactionToChain(normalTx.GetHash());

    int someNumberOfBlocksToAdd = GetRand(25)+1;
    int startingNumberOfConfirmations = 1;
    for(int numberOfAdditionalBlocks = 0; numberOfAdditionalBlocks < someNumberOfBlocksToAdd ; ++numberOfAdditionalBlocks )
    {
        BOOST_CHECK_EQUAL(normalTx.GetNumberOfBlockConfirmations(),startingNumberOfConfirmations+numberOfAdditionalBlocks);
        extendFakeChainAndGetTipBlockHash();
    }
}


#define BOOST_CHECK_EQUAL_MESSAGE(L, R, M)      { BOOST_TEST_INFO(M); BOOST_CHECK_EQUAL(L, R); }

BOOST_AUTO_TEST_CASE(willReturnZeroBalancesWhenTransactionsAreUnconfirmed)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    CScript ownedVaultScript = vaultScriptAsOwner();
    CScript managedVaultScript = vaultScriptAsManager();

    unsigned outputIndex=0;
    AddDefaultTxToWallet(normalScript,outputIndex,100*COIN);
    AddDefaultTxToWallet(ownedVaultScript,outputIndex,1000*COIN);
    AddDefaultTxToWallet(managedVaultScript,outputIndex,10000*COIN);

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), 0,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), 0,"Staking balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetSpendableBalance(), 0,"Spendable balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willReturnCorrectBalanceWhenTransactionIsConfirmed)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    unsigned outputIndex=0;
    const CWalletTx& normalTx = AddDefaultTxToWallet(normalScript,outputIndex,100*COIN);
    FakeAddTransactionToChain(normalTx.GetHash());

    BOOST_CHECK_MESSAGE(normalTx.GetNumberOfBlockConfirmations() >= 1,"Transaction is not at least one block deep!");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), 100*COIN,"Total balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willFindStakingBalanceMatchesBalanceWhenOwnWalletFundsAreNotVaulted)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    CScript managedVaultScript = vaultScriptAsManager();
    currentWallet.AddCScript(managedVaultScript);

    unsigned outputIndex=0;
    const CWalletTx& normalTx = AddDefaultTxToWallet(normalScript,outputIndex,100*COIN);
    const CWalletTx& managedVaultTx = AddDefaultTxToWallet(managedVaultScript,outputIndex,10000*COIN);
    FakeAddTransactionToChain(normalTx.GetHash());
    FakeAddTransactionToChain(managedVaultTx.GetHash());

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), 10100*COIN,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), 10100*COIN,"Staking balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willHaveAStakingBalanceFromAVaultThatIsntSpendable)
{
    CScript managedVaultScript = vaultScriptAsManager();
    currentWallet.AddCScript(managedVaultScript);

    unsigned outputIndex=0;
    const CWalletTx& managedVaultTx = AddDefaultTxToWallet(managedVaultScript,outputIndex,10000*COIN);
    FakeAddTransactionToChain(managedVaultTx.GetHash());

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), 10000*COIN,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), 10000*COIN,"Staking balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetSpendableBalance(), 0*COIN,"Spendable balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willTreatOwnedVaultAsUnspendableButWillRecordBalance)
{
    CScript ownedVaultScript = vaultScriptAsOwner();

    unsigned outputIndex=0;
    const CWalletTx& ownedVaultTx = AddDefaultTxToWallet(ownedVaultScript,outputIndex,10000*COIN);
    FakeAddTransactionToChain(ownedVaultTx.GetHash());

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), 10000*COIN,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), 0*COIN,"Staking balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetSpendableBalance(), 0*COIN,"Spendable balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willEnsureBalancesAreAsExpected)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    CScript ownedVaultScript = vaultScriptAsOwner();
    CScript managedVaultScript = vaultScriptAsManager();
    currentWallet.AddCScript(managedVaultScript);


    CAmount normalTxValue = (GetRand(1000)+1)*COIN;
    CAmount ownedVaultTxValue = (GetRand(1000)+1)*COIN;
    CAmount managedVaultTxValue = (GetRand(1000)+1)*COIN;
    CAmount totalBalance = normalTxValue + ownedVaultTxValue + managedVaultTxValue;

    unsigned outputIndex=0;
    const CWalletTx& normalTx = AddDefaultTxToWallet(normalScript,outputIndex,normalTxValue);
    const CWalletTx& ownedVaultTx = AddDefaultTxToWallet(ownedVaultScript,outputIndex,ownedVaultTxValue);
    const CWalletTx& managedVaultTx = AddDefaultTxToWallet(managedVaultScript,outputIndex,managedVaultTxValue);

    FakeAddTransactionToChain(normalTx.GetHash());
    FakeAddTransactionToChain(ownedVaultTx.GetHash());
    FakeAddTransactionToChain(managedVaultTx.GetHash());

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), totalBalance,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), normalTxValue+managedVaultTxValue,"Staking balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetSpendableBalance(), normalTxValue,"Spendable balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalanceByCoinType(OWNED_VAULT_COINS), ownedVaultTxValue,"Owned vault balance was not the expected amount");
}

BOOST_AUTO_TEST_CASE(willEnsureLockedCoinsDoNotCountTowardStakableBalance)
{
    CScript normalScript = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
    CScript ownedVaultScript = vaultScriptAsOwner();
    CScript managedVaultScript = vaultScriptAsManager();
    currentWallet.AddCScript(managedVaultScript);


    CAmount firstNormalTxValue = (GetRand(1000)+1)*COIN;
    CAmount secondNormalTxValue = (GetRand(1000)+1)*COIN;

    unsigned firstTxOutputIndex=0;
    const CWalletTx& firstNormalTx = AddDefaultTxToWallet(normalScript,firstTxOutputIndex,firstNormalTxValue);
    unsigned secondTxOutputIndex=0;
    const CWalletTx& secondNormalTx = AddDefaultTxToWallet(normalScript,secondTxOutputIndex,secondNormalTxValue);
    FakeAddTransactionToChain(firstNormalTx.GetHash());
    FakeAddTransactionToChain(secondNormalTx.GetHash());

    currentWallet.LockCoin(COutPoint(firstNormalTx.GetHash(),firstTxOutputIndex));

    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetBalance(), firstNormalTxValue+secondNormalTxValue,"Total balance was not the expected amount");
    BOOST_CHECK_EQUAL_MESSAGE(currentWallet.GetStakingBalance(), secondNormalTxValue,"Staking balance was not the expected amount");
}

BOOST_AUTO_TEST_SUITE_END()
