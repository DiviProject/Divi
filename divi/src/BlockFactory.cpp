#include <BlockFactory.h>
#include <script/script.h>
#include <BlockTemplate.h>
#include <I_BlockSubsidyProvider.h>
#include <I_BlockTransactionCollector.h>
#include <I_BlockProofProver.h>
#include <timedata.h>
#include <boost/thread.hpp>
#include <chain.h>
#include <chainparams.h>
#include <sync.h>
#include <Logging.h>
#include <script/standard.h>
#include <Settings.h>
#include <utiltime.h>
#include <ThreadManagementHelpers.h>
#include <I_DifficultyAdjuster.h>

// Actual mining functions
BlockFactory::BlockFactory(
    const Settings& settings,
    const CChainParams& chainParameters,
    const CChain& chain,
    const I_DifficultyAdjuster& difficultyAdjuster,
    const I_BlockProofProver& blockProofProver,
    I_BlockTransactionCollector& blockTransactionCollector
    ): settings_(settings)
    , chainParameters_(chainParameters)
    , chain_(chain)
    , difficultyAdjuster_(difficultyAdjuster)
    , blockProofProver_( blockProofProver)
    , blockTransactionCollector_(blockTransactionCollector)
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

void BlockFactory::SetBlockHeader(
    CBlockHeader& block,
    const CBlockIndex* previousBlockIndex) const
{
    // Fill in header
    block.nBits = difficultyAdjuster_.computeNextBlockDifficulty(previousBlockIndex);
    block.nTime = GetAdjustedTime();
    block.hashPrevBlock = previousBlockIndex->GetBlockHash();
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
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
    pblocktemplate->block.vtx.push_back(
        fProofOfStake
        ?CreateEmptyCoinbaseTransaction(nextBlockHeight)
        :CreateCoinbaseTransaction(nextBlockHeight,scriptPubKeyIn));
    assert(pblocktemplate->block.vtx.size()==1);
    assert(pblocktemplate->block.vtx[0].vout[0].nValue==0);

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

    boost::this_thread::interruption_point();
    if(!blockProofProver_.attachBlockProof(pblocktemplate->previousBlockIndex, fProofOfStake, pblocktemplate->block))
        return NULL;

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