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

class MultiWalletTestFixture
{
public:
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
private:
    std::vector<std::unique_ptr<CWallet>> walletCache_;
    const int32_t version;
    int32_t timestamp;
    CChain activeChain_;
    BlockMap blockIndices_;
    FakeBlockIndexChain fakeChain_;
public:
    static void EnableSmallHDWallet(CWallet& wallet)
    {
        wallet.defaultKeyPoolTopUp = (int64_t)1;
        bool firstLoad = true;
        wallet.LoadWallet(firstLoad);
        wallet.GenerateNewHDChain();
        wallet.SetMinVersion(FEATURE_HD);
        CPubKey newDefaultKey;
        wallet.GetKeyFromPool(newDefaultKey, false);
        wallet.SetDefaultKey(newDefaultKey);
    }
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
    MultiWalletTestFixture(
        ): walletCache_()
        , version(0x01)
        , timestamp(1600000000)
        , activeChain_()
        , blockIndices_()
        , fakeChain_()
    {
        extendFakeChainAndGetTipBlockHash(); // Fake genesis block
    }

    void createWallets(const unsigned walletCount)
    {
        if(!walletCache_.empty()) return;

        walletCache_.resize(walletCount);
        for(unsigned walletID = 0; walletID < walletCount; ++ walletID)
        {
            auto& walletUniquePtr = walletCache_[walletID];
            walletUniquePtr.reset(new CWallet("wallet"+std::to_string(walletID)+".dat"));
            EnableSmallHDWallet(*walletUniquePtr);
        }
    }
    std::string deallocateWalletAndGetName(const unsigned walletID)
    {
        if(walletCache_.empty() || walletID >= walletCache_.size() || !walletCache_[walletID].get()) return "";
        std::string walletName = walletCache_[walletID]->strWalletFile;
        walletCache_[walletID].reset();
        return walletName;
    }

    const CWalletTx& AddDefaultTxToWallet(CWallet& currentWallet, CAmount amount)
    {
        assert(currentWallet.vchDefaultKey.IsValid());
        CScript scriptToPayTo = GetScriptForDestination(currentWallet.vchDefaultKey.GetID());
        unsigned outputIndex =0;
        CMutableTransaction tx = createDefaultTransaction(scriptToPayTo,outputIndex,amount);
        CMerkleTx merkleTx(tx,activeChain_,blockIndices_);
        CWalletTx wtx(merkleTx);
        currentWallet.AddToWallet(wtx);
        const CWalletTx* txPtr = currentWallet.GetWalletTx(wtx.GetHash());
        assert(txPtr);
        return *txPtr;
    }
    CWallet& getWallet(const unsigned walletID) const
    {
        if(walletID < walletCache_.size())
        {
            return *walletCache_[walletID];
        }
        assert(false);
    }
    void FakeAddTransactionToChain(CWallet& currentWallet,const uint256& txHash)
    {
        CWalletTx* txPtr = const_cast<CWalletTx*>( currentWallet.GetWalletTx(txHash) );
        txPtr->hashBlock = extendFakeChainAndGetTipBlockHash();
        txPtr->nIndex = 0;
        txPtr->fMerkleVerified = true;
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
            const CWalletTx& tx = AddDefaultTxToWallet(getWallet(walletID),100*COIN);
            FakeAddTransactionToChain(getWallet(walletID),tx.GetHash());
        }
        BOOST_CHECK_EQUAL_MESSAGE(
            getWallet(walletID).GetBalance(),
            CAmount(totalTxsPerWallet*100*COIN),
            "Balance is different from expected!");
    }
}

BOOST_AUTO_TEST_SUITE_END()