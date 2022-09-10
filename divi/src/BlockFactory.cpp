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

static CMutableTransaction CreateEmptyCoinbaseTransaction(const unsigned int blockHeight)
{
    /** Constant stuff for coinbase transactions we create: */
    static CScript COINBASE_FLAGS;
    constexpr unsigned int nExtraNonce = 1u;

    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << blockHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout.back().SetEmpty();
    assert(CTransaction(txNew).IsCoinBase());
    return txNew;
}
static CMutableTransaction CreateCoinbaseTransaction(const unsigned int blockHeight,const CScript& scriptPubKeyIn)
{
    CMutableTransaction txNew = CreateEmptyCoinbaseTransaction(blockHeight);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
    return txNew;
}
static CMutableTransaction CreateDummyCoinstakeTransaction()
{
    static uint256 dummyTxHash = uint256S("0x64");
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vout.resize(2);
    txNew.vin[0].prevout = COutPoint(dummyTxHash,0);
    txNew.vout[0].SetEmpty();
    assert(CTransaction(txNew).IsCoinStake());
    return txNew;
}
bool BlockFactory::AppendProofOfStakeToBlock(
    CBlockTemplate& pBlockTemplate)
{
    CBlock& block = pBlockTemplate.block;
    CMutableTransaction txCoinStake;
    unsigned int nTxNewTime = block.nTime;
    if(coinstakeCreator_.CreateProofOfStake(
            pBlockTemplate.previousBlockIndex,
            block.nBits,
            txCoinStake,
            nTxNewTime))
    {
        block.nTime = nTxNewTime;
        block.vtx[1]=txCoinStake;
        return true;
    }

    return false;
}

void BlockFactory::UpdateTime(CBlockHeader& block, const CBlockIndex* pindexPrev) const
{
    block.nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());
}

void BlockFactory::SetBlockHeader(
    CBlockHeader& block,
    const CBlockIndex* previousBlockIndex) const
{
    // Fill in header
    block.nBits = GetNextWorkRequired(previousBlockIndex, chainParameters_);
    block.nTime = GetAdjustedTime();
    block.hashPrevBlock = previousBlockIndex->GetBlockHash();
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
}

static void SetCoinbaseTransactionAndRewards(
    CBlock& block,
    CMutableTransaction& coinbaseTx,
    const CAmount reward)
{
    coinbaseTx.vout[0].nValue = reward;
    block.vtx.push_back(coinbaseTx);
    assert(block.vtx.size()==1);
}

bool BlockFactory::AppendProofOfWorkToBlock(
    CBlockTemplate& blocktemplate)
{
    CBlock& block = blocktemplate.block;
    const CBlockIndex* const previousBlockIndex = blocktemplate.previousBlockIndex;
    UpdateTime(block, previousBlockIndex);
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
                LogPrint("minting","%s: proof-of-work found  \n  hash: %s  \ntarget: %s\n",__func__, hash, hashTarget);
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
        if (previousBlockIndex != chain_.Tip())
            break;

        // Update nTime every few seconds
        UpdateTime(block, previousBlockIndex);
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
    const unsigned nextBlockHeight = pblocktemplate->previousBlockIndex->nHeight+1;
    pblocktemplate->coinbaseTransaction =
        std::make_shared<CMutableTransaction>(
            fProofOfStake
            ?CreateEmptyCoinbaseTransaction(nextBlockHeight)
            :CreateCoinbaseTransaction(nextBlockHeight,scriptPubKeyIn));

    SetCoinbaseTransactionAndRewards(
        pblocktemplate->block,
        *(pblocktemplate->coinbaseTransaction),
        (!fProofOfStake)? blockSubsidies_.GetBlockSubsidity(nextBlockHeight).nStakeReward: 0);

    if(fProofOfStake)
    {
        assert(pblocktemplate->block.vtx.size()==1);
        pblocktemplate->block.vtx.push_back(CreateDummyCoinstakeTransaction());
        assert(pblocktemplate->block.IsProofOfStake());
    }

    SetBlockHeader(pblocktemplate->block,pblocktemplate->previousBlockIndex);
    // Collect memory pool transactions into the block
    if(!blockTransactionCollector_.CollectTransactionsIntoBlock(*pblocktemplate))
    {
        return NULL;
    }
    if (fProofOfStake) {
        boost::this_thread::interruption_point();

        if (!AppendProofOfStakeToBlock(*pblocktemplate))
            return NULL;
        pblocktemplate->block.hashMerkleRoot = pblocktemplate->block.BuildMerkleTree();
    }
    else
    {
        pblocktemplate->block.hashMerkleRoot = pblocktemplate->block.BuildMerkleTree();
        boost::this_thread::interruption_point();
        if (!AppendProofOfWorkToBlock(*pblocktemplate))
            return NULL;
    }

    LogPrint("minting","%s: releasing template\n", __func__);
    return pblocktemplate.release();
}

CBlockTemplate* BlockFactory::CreateNewPoWBlock(const CScript& scriptPubKey)
{
    constexpr bool isBlockProofOfStake = false;
    return CreateNewBlock(scriptPubKey, isBlockProofOfStake);
};
CBlockTemplate* BlockFactory::CreateNewPoSBlock()
{
    constexpr bool isBlockProofOfStake = true;
    return CreateNewBlock(CScript(), isBlockProofOfStake);
};