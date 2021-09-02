#include <BlockFactory.h>
#include <script/script.h>
#include <BlockTemplate.h>
#include <I_BlockSubsidyProvider.h>
#include <I_BlockTransactionCollector.h>
#include <I_PoSTransactionCreator.h>
#include <timedata.h>
#include <boost/thread.hpp>
#include <pow.h>
#include <chain.h>
#include <chainparams.h>
#include <reservekey.h>
#include <sync.h>
#include <Logging.h>
#include <script/standard.h>
#include <Settings.h>
#include <utiltime.h>
#include <ThreadManagementHelpers.h>

// Actual mining functions
BlockFactory::BlockFactory(
    const I_BlockSubsidyProvider& blockSubsidies,
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    const Settings& settings,
    const CChain& chain,
    const CChainParams& chainParameters
    ): settings_(settings)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , blockSubsidies_(blockSubsidies)
    , blockTransactionCollector_(blockTransactionCollector)
    , coinstakeCreator_( coinstakeCreator)
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
    SetRequiredWork(pBlockTemplate);
    SetBlockTime(block);

    CMutableTransaction txCoinStake;
    unsigned int nTxNewTime = block.nTime;
    if(coinstakeCreator_.CreateProofOfStake(
            pBlockTemplate.previousBlockIndex,
            block.nBits,
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

void BlockFactory::UpdateTime(CBlockHeader& block, const CBlockIndex* pindexPrev) const
{
    block.nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (chainParameters_.AllowMinDifficultyBlocks())
        block.nBits = GetNextWorkRequired(pindexPrev, &block,chainParameters_);
}

void BlockFactory::IncrementExtraNonce(
    CBlock& block,
    const CBlockIndex* pindexPrev,
    unsigned int& nExtraNonce) const
{
    /** Constant stuff for coinbase transactions we create: */
    static CScript COINBASE_FLAGS;
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != block.hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = block.hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(block.vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    block.vtx[0] = txCoinbase;
    block.hashMerkleRoot = block.BuildMerkleTree();
}

void BlockFactory::SetBlockHeaders(
    CBlockTemplate& pblocktemplate,
    const bool& proofOfStake) const
{
    // Fill in header
    CBlock& block = pblocktemplate.block;
    block.hashPrevBlock = pblocktemplate.previousBlockIndex->GetBlockHash();
    if (!proofOfStake)
        UpdateTime(block, pblocktemplate.previousBlockIndex);
    block.nBits = GetNextWorkRequired(pblocktemplate.previousBlockIndex, &block, chainParameters_);
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
}

void BlockFactory::SetCoinbaseRewardAndHeight (
    CBlockTemplate& pblocktemplate,
    const bool& fProofOfStake) const
{
    // Compute final coinbase transaction.
    int nHeight = pblocktemplate.previousBlockIndex->nHeight+1;
    CBlock& block = pblocktemplate.block;
    CMutableTransaction& coinbaseTx = *pblocktemplate.coinbaseTransaction;
    block.vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
    if (!fProofOfStake) {
        coinbaseTx.vout[0].nValue = blockSubsidies_.GetBlockSubsidity(nHeight).nStakeReward;
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
        block.vtx[0] = coinbaseTx;
    }
}

void BlockFactory::UpdateBlockCoinBaseAndHeaders (
    CBlockTemplate& blocktemplate,
    const bool& fProofOfStake) const
{
    unsigned int extraNonce = 0u;
    CBlock& block = blocktemplate.block;
    SetCoinbaseRewardAndHeight(blocktemplate, fProofOfStake);
    SetBlockHeaders(blocktemplate, fProofOfStake);
    IncrementExtraNonce(block, blocktemplate.previousBlockIndex, extraNonce);
}

bool BlockFactory::AppendProofOfWorkToBlock(
    CBlockTemplate& blocktemplate)
{
    CBlock& block = blocktemplate.block;
    int64_t nStart = GetTime();
    uint256 hashTarget = uint256().SetCompact(block.nBits);
    while (true)
    {
        unsigned int nHashesDone = 0;
        uint256 hash;
        while (true)
        {
            hash = block.GetHash();
            if (hash <= hashTarget)
            {
                // Found a solution
                LogPrintf("%s:\n",__func__);
                LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash, hashTarget);
                return true;
            }
            block.nNonce += 1;
            nHashesDone += 1;
            if ((block.nNonce & 0xFF) == 0)
                break;
        }

        // Check for stop or if block needs to be rebuilt
        boost::this_thread::interruption_point();

        if (block.nNonce >= 0xffff0000)
            break;
        if (GetTime() - nStart > 60)
            break;
        if (blocktemplate.previousBlockIndex != chain_.Tip())
            break;

        // Update nTime every few seconds
        UpdateTime(block, blocktemplate.previousBlockIndex);
        if (chainParameters_.AllowMinDifficultyBlocks())
        {
            // Changing block->nTime can change work required on testnet:
            hashTarget.SetCompact(block.nBits);
        }
    }
    return false;
}

CBlockTemplate* BlockFactory::CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake)
{
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get())
        return NULL;

    // Maybe override the block version, for fork tests.
    if (chainParameters_.MineBlocksOnDemand())
    {
        auto& block = pblocktemplate->block;
        block.nVersion = settings_.GetArg("-blockversion", block.nVersion);
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
    if(!blockTransactionCollector_.CollectTransactionsIntoBlock(*pblocktemplate))
    {
        return NULL;
    }

    UpdateBlockCoinBaseAndHeaders(*pblocktemplate,fProofOfStake);

    LogPrintf("CreateNewBlock(): releasing template %s\n", "");
    return pblocktemplate.release();
}

CBlockTemplate* BlockFactory::CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake)
{
    CPubKey pubkey;
    if (!reservekey.GetReservedKey(pubkey, false))
        return NULL;

    CScript scriptPubKey = (fProofOfStake)? CScript() : GetScriptForDestination(pubkey.GetID());
    return CreateNewBlock(scriptPubKey, fProofOfStake);
}
