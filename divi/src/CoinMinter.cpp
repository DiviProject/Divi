#include <CoinMinter.h>

#include <wallet.h>
#include <utiltime.h>
#include <chain.h>
#include <chainparams.h>
#include <PeerNotificationOfMintService.h>
#include <masternode-sync.h>
#include <utilmoneystr.h>
#include <timedata.h>
#include <boost/thread.hpp>
#include <I_BlockFactory.h>
#include <BlockTemplate.h>
#include <BlockSigning.h>
#include <ValidationState.h>
#include <txmempool.h>
#include <I_BlockSubsidyProvider.h>
#include <Logging.h>
#include <MasternodeHelpers.h>
#include <ThreadManagementHelpers.h>

constexpr int hashingDelay = 45;
bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);

CoinMinter::CoinMinter(
    const I_BlockSubsidyProvider& blockSubsidies,
    I_BlockFactory& blockFactory,
    CWallet* pwallet,
    const CChain& chain,
    const CChainParams& chainParameters,
    std::vector<CNode*>& peers,
    const CMasternodeSync& masternodeSynchronization,
    HashedBlockMap& mapHashedBlocks,
    CTxMemPool& transactionMemoryPool,
    AnnotatedMixin<boost::recursive_mutex>& mainCS
    ): blockSubsidies_( blockSubsidies )
    , blockFactory_( blockFactory )
    , peerNotifier_( std::make_shared<PeerNotificationOfMintService>(peers))
    , mintingIsRequested_(false)
    , pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , mempool_(transactionMemoryPool)
    , mainCS_(mainCS)
    , masternodeSync_(masternodeSynchronization)
    , mapHashedBlocks_(mapHashedBlocks)
    , haveMintableCoins_(false)
    , lastTimeCheckedMintable_(0)
    , timeToWait_(0)
{
}

bool CoinMinter::hasMintableCoinForProofOfStake()
{
    int timeWaited = GetTime() - lastTimeCheckedMintable_;

    if(timeWaited > fiveMinutes_)
    {
        lastTimeCheckedMintable_ = GetTime();
        haveMintableCoins_ = pwallet_->MintableCoins();
    }
    else
    {
        timeToWait_ = fiveMinutes_ - timeWaited;
    }
    return haveMintableCoins_;
}
bool CoinMinter::nextBlockIsProofOfStake() const
{
    CBlockIndex* chainTip = chain_.Tip();
    return ( (chainTip? chainTip->nHeight : 0) >= chainParameters_.LAST_POW_BLOCK() );
}

bool CoinMinter::satisfiesMintingRequirements() const
{
    const unsigned oneReorgWorthOfTimestampDrift = 60*chainParameters_.MaxReorganizationDepth();
    const unsigned minimumChainTipTimestampForMinting = GetTime() - oneReorgWorthOfTimestampDrift;

    CBlockIndex* chainTip = chain_.Tip();
    bool chainTipNotSyncedEnough = chainTip? chainTip->nTime < minimumChainTipTimestampForMinting: IsBlockchainSynced();
    bool stakingRequirementsAreMet =
        !(
            chainTipNotSyncedEnough ||
            !peerNotifier_->havePeersToNotify() ||
            pwallet_->IsLocked() ||
            pwallet_->GetStakingBalance() <= 0 ||
            !masternodeSync_.IsSynced()
        );
    return stakingRequirementsAreMet;
}
bool CoinMinter::limitStakingSpeed() const
{
    CBlockIndex* chainTip = chain_.Tip();
    if (chainTip && mapHashedBlocks_.count(chainTip->nHeight)) //search our map of hashed blocks, see if bestblock has been hashed yet
    {
        if (GetTime() - mapHashedBlocks_[chainTip->nHeight] < static_cast<int64_t>(hashingDelay)/2 )
        {
            return true;
        }
    }
    return false;
}

bool CoinMinter::CanMintCoins()
{
    if( !hasMintableCoinForProofOfStake() ||
        !nextBlockIsProofOfStake() ||
        !satisfiesMintingRequirements() ||
        limitStakingSpeed())
    {
        return false;
    }
    return true;
}

void CoinMinter::sleep(uint64_t milliseconds) const
{
    MilliSleep(milliseconds);
}
void CoinMinter::setMintingRequestStatus(bool newStatus)
{
    mintingIsRequested_ = newStatus;
}
bool CoinMinter::mintingHasBeenRequested() const
{
    return mintingIsRequested_;
}

void CoinMinter::UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev) const
{
    block->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (chainParameters_.AllowMinDifficultyBlocks())
        block->nBits = GetNextWorkRequired(pindexPrev, block,chainParameters_);
}

bool CoinMinter::ProcessBlockFound(CBlock* block, CReserveKey& reservekey) const
{
    LogPrintf("%s\n", *block);
    LogPrintf("generated %s\n", FormatMoney(block->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(mainCS_);
        if (block->hashPrevBlock != chain_.Tip()->GetBlockHash())
            return error("DIVIMiner : generated block is stale");
    }

    // Remove key from key pool
    reservekey.KeepKey();

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, NULL, block))
        return error("DIVIMiner : ProcessNewBlock, block not accepted");

    peerNotifier_->notifyPeers(block->GetHash());

    return true;
}

void CoinMinter::IncrementExtraNonce(CBlock* block,const CBlockIndex* pindexPrev, unsigned int& nExtraNonce) const
{
    /** Constant stuff for coinbase transactions we create: */
    static CScript COINBASE_FLAGS;
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != block->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = block->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(block->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    block->vtx[0] = txCoinbase;
    block->hashMerkleRoot = block->BuildMerkleTree();
}


void CoinMinter::SetCoinbaseRewardAndHeight (
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

void CoinMinter::SetBlockHeaders(CBlockTemplate& pblocktemplate, const bool& proofOfStake) const
{
    // Fill in header
    CBlock& block = pblocktemplate.block;
    block.hashPrevBlock = pblocktemplate.previousBlockIndex->GetBlockHash();
    if (!proofOfStake)
        UpdateTime(&block, pblocktemplate.previousBlockIndex);
    block.nBits = GetNextWorkRequired(pblocktemplate.previousBlockIndex, &block, chainParameters_);
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
}

bool CoinMinter::createProofOfStakeBlock(
    unsigned int nExtraNonce,
    CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = true;
    bool blockSuccessfullyCreated = false;
    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_.CreateNewBlockWithKey(reserveKey, fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    const CBlockIndex* pindexPrev = pblocktemplate->previousBlockIndex;

    CBlock* block = &pblocktemplate->block;
    SetCoinbaseRewardAndHeight(*pblocktemplate, fProofOfStake);
    SetBlockHeaders(*pblocktemplate, fProofOfStake);
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    //Stake miner main
    LogPrintf("%s: proof-of-stake block found %s \n",__func__, block->GetHash());

    if (!SignBlock(*pwallet_, *block)) {
        LogPrintf("%s: Signing new block failed \n",__func__);
        return false;
    }

    LogPrintf("%s: proof-of-stake block was signed %s \n", __func__, block->GetHash());
    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    blockSuccessfullyCreated = ProcessBlockFound(block, reserveKey);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    return blockSuccessfullyCreated;
}

bool CoinMinter::createProofOfWorkBlock(
    unsigned int nExtraNonce,
    CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = false;
    bool blockSuccessfullyCreated = false;
    unsigned int nTransactionsUpdatedLast = mempool_.GetTransactionsUpdated();

    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_.CreateNewBlockWithKey(reserveKey, fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    const CBlockIndex* pindexPrev = pblocktemplate->previousBlockIndex;

    CBlock* block = &pblocktemplate->block;
    SetCoinbaseRewardAndHeight(*pblocktemplate, fProofOfStake);
    SetBlockHeaders(*pblocktemplate, fProofOfStake);
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    LogPrintf("Running DIVIMiner with %u transactions in block (%u bytes)\n", block->vtx.size(),
                ::GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION));

    int64_t nStart = GetTime();
    uint256 hashTarget = uint256().SetCompact(block->nBits);
    while (true)
    {
        unsigned int nHashesDone = 0;
        blockSuccessfullyCreated = false;
        uint256 hash;
        while (true) {
            hash = block->GetHash();
            if (hash <= hashTarget)
            {
                // Found a solution
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                blockSuccessfullyCreated = ProcessBlockFound(block, reserveKey);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);
                LogPrintf("%s:\n",__func__);
                LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash, hashTarget);
                // In regression test mode, stop mining after a block is found. This
                // allows developers to controllably generate a block on demand.
                if (chainParameters_.MineBlocksOnDemand())
                    return blockSuccessfullyCreated;

                break;
            }
            block->nNonce += 1;
            nHashesDone += 1;
            if ((block->nNonce & 0xFF) == 0)
                break;
        }

        // Check for stop or if block needs to be rebuilt
        boost::this_thread::interruption_point();
        // Regtest mode doesn't require peers
        if (peerNotifier_->havePeersToNotify() && chainParameters_.MiningRequiresPeers())
            break;
        if (block->nNonce >= 0xffff0000)
            break;
        if (mempool_.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
            break;
        if (pindexPrev != chain_.Tip())
            break;

        // Update nTime every few seconds
        UpdateTime(block, pindexPrev);
        if (chainParameters_.AllowMinDifficultyBlocks())
        {
            // Changing block->nTime can change work required on testnet:
            hashTarget.SetCompact(block->nBits);
        }
    }
    return blockSuccessfullyCreated;
}

bool CoinMinter::createNewBlock(
    unsigned int nExtraNonce,
    bool fProofOfStake) const
{
    CReserveKey reserveKey(*pwallet_);
    if(fProofOfStake)
        return createProofOfStakeBlock(nExtraNonce, reserveKey);

    return createProofOfWorkBlock(nExtraNonce, reserveKey);
}
