#include <BlockFactory.h>
#include <script/script.h>
#include <wallet.h>
#include <BlockTemplate.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <PoSTransactionCreator.h>
#include <timedata.h>

// Actual mining functions
BlockFactory::BlockFactory(
    CWallet& wallet,
    int64_t& lastCoinstakeSearchInterval,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    CChain& chain,
    const CChainParams& chainParameters,
    CTxMemPool& transactionMemoryPool,
    AnnotatedMixin<boost::recursive_mutex>& mainCS
    ): wallet_(wallet)
    , lastCoinstakeSearchInterval_(lastCoinstakeSearchInterval)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , mempool_(transactionMemoryPool)
    , mainCS_(mainCS)
    , blockTransactionCollector_(std::make_shared<BlockMemoryPoolTransactionCollector>(mempool_,mainCS_))
    , coinstakeCreator_( std::make_shared<PoSTransactionCreator>(wallet_, lastCoinstakeSearchInterval_,hashedBlockTimestamps))
{

}


void BlockFactory::SetRequiredWork(CBlockTemplate& pBlockTemplate)
{
    CBlock& block = pBlockTemplate.block;
    block.nBits = GetNextWorkRequired(pBlockTemplate.previousBlockIndex, &block,chainParameters_);
}

void BlockFactory::SetBlockTime(CBlock& block)
{
    block.nTime = GetAdjustedTime();
}

void BlockFactory::SetCoinbaseTransactionAndDefaultFees(
    CBlockTemplate& pblocktemplate,
    const CMutableTransaction& coinbaseTransaction)
{
    pblocktemplate.block.vtx.push_back(coinbaseTransaction);
}

void BlockFactory::CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& txNew)
{
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
}

bool BlockFactory::AppendProofOfStakeToBlock(
    CBlockTemplate& pBlockTemplate)
{
    CBlock& block = pBlockTemplate.block;
    CMutableTransaction txCoinStake;
    SetRequiredWork(pBlockTemplate);
    SetBlockTime(block);

    // ppcoin: if coinstake available add coinstake tx
    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // only initialized at startup

    unsigned int nTxNewTime = 0;
    if(coinstakeCreator_->CreateProofOfStake(
            block.nBits,
            block.nTime,
            nLastCoinStakeSearchTime,
            txCoinStake,
            nTxNewTime))
    {
        block.nTime = nTxNewTime;
        block.vtx[0].vout[0].SetEmpty();
        block.vtx.push_back(CTransaction(txCoinStake));
        return true;
    }

    return false;
}

CBlockTemplate* BlockFactory::CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake)
{
    LOCK(mainCS_);
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get())
        return NULL;

    // Maybe override the block version, for fork tests.
    if (chainParameters_.MineBlocksOnDemand()) {
        auto& block = pblocktemplate->block;
        block.nVersion = GetArg("-blockversion", block.nVersion);
    }

    pblocktemplate->previousBlockIndex = chain_.Tip();
    if(!pblocktemplate->previousBlockIndex) return NULL;

    // Create coinbase tx
    pblocktemplate->coinbaseTransaction = std::make_shared<CMutableTransaction>();
    CMutableTransaction& coinbaseTransaction = *pblocktemplate->coinbaseTransaction;
    CreateCoinbaseTransaction(scriptPubKeyIn, coinbaseTransaction);

    SetCoinbaseTransactionAndDefaultFees(*pblocktemplate, coinbaseTransaction);

    if (fProofOfStake) {
        boost::this_thread::interruption_point();

        if (!AppendProofOfStakeToBlock(*pblocktemplate))
            return NULL;
    }

    // Collect memory pool transactions into the block

    if(!blockTransactionCollector_->CollectTransactionsIntoBlock(
            *pblocktemplate,
            fProofOfStake,
            coinbaseTransaction
        ))
    {
        return NULL;
    }

    LogPrintf("CreateNewBlock(): releasing template %s\n", "");
    return pblocktemplate.release();
}

CBlockTemplate* BlockFactory::CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake)
{
    CPubKey pubkey;
    if (!reservekey.GetReservedKey(pubkey, false))
        return NULL;

    CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    return CreateNewBlock(scriptPubKey, fProofOfStake);
}
