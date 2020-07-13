#include <BlockFactory.h>
#include <script/script.h>
#include <wallet.h>
#include <BlockTemplate.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <CoinstakeCreator.h>
#include <timedata.h>

// Actual mining functions
BlockFactory::BlockFactory(
    CWallet& wallet,
    int64_t& lastCoinstakeSearchInterval,
    CChain& chain, 
    const CChainParams& chainParameters
    ): wallet_(wallet)
    , lastCoinstakeSearchInterval_(lastCoinstakeSearchInterval)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , blockTransactionCollector_(std::make_shared<BlockMemoryPoolTransactionCollector>(mempool,cs_main))
    , coinstakeCreator_( std::make_shared<CoinstakeCreator>(wallet_, lastCoinstakeSearchInterval_))
{

}


void BlockFactory::SetRequiredWork(CBlock& block)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = chain_.Tip();
    block.nBits = GetNextWorkRequired(pindexPrev, &block,chainParameters_);
}

void BlockFactory::SetBlockTime(CBlock& block)
{
    block.nTime = GetAdjustedTime();
}

void BlockFactory::SetCoinbaseTransactionAndDefaultFees(
    std::unique_ptr<CBlockTemplate>& pblocktemplate, 
    const CMutableTransaction& coinbaseTransaction)
{
    pblocktemplate->block.vtx.push_back(coinbaseTransaction);
    pblocktemplate->vTxFees.push_back(-1);   // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end
}

void BlockFactory::CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& txNew)
{
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
}

bool BlockFactory::AppendProofOfStakeToBlock(
    CBlock& block)
{
    CMutableTransaction txCoinStake;
    SetRequiredWork(block);
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
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get())
        return NULL;
    CBlock& block = pblocktemplate->block; // pointer for convenience

    // Create coinbase tx
    pblocktemplate->coinbaseTransaction = std::make_shared<CMutableTransaction>();
    CMutableTransaction& coinbaseTransaction = *pblocktemplate->coinbaseTransaction;
    CreateCoinbaseTransaction(scriptPubKeyIn, coinbaseTransaction);
    
    SetCoinbaseTransactionAndDefaultFees(pblocktemplate, coinbaseTransaction);

    if (fProofOfStake) {
        boost::this_thread::interruption_point();

        if (!AppendProofOfStakeToBlock(block))
            return NULL;
    }

    // Collect memory pool transactions into the block

    if(!blockTransactionCollector_->CollectTransactionsIntoBlock(
            pblocktemplate,
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