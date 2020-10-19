#include <CoinMinter.h>
#include <wallet.h>
#include <utiltime.h>
#include <chain.h>
#include <chainparams.h>
#include <PeerNotificationOfMintService.h>
#include <masternode-sync.h>
#include <BlockMemoryPoolTransactionCollector.h>
#include <utilmoneystr.h>
#include <timedata.h>
#include <PoSTransactionCreator.h>
#include <boost/thread.hpp>
#include <SuperblockHelpers.h>
#include <BlockFactory.h>
#include <ValidationState.h>
#include <txmempool.h>

extern const int nHashDrift;
extern int64_t nReserveBalance;

extern CScript COINBASE_FLAGS;

bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);

CoinMinter::CoinMinter(
    CWallet* pwallet,
    CChain& chain,
    const CChainParams& chainParameters,
    std::vector<CNode*>& peers,
    CMasternodeSync& masternodeSynchronization,
    HashedBlockMap& mapHashedBlocks,
    CTxMemPool& transactionMemoryPool,
    AnnotatedMixin<boost::recursive_mutex>& mainCS,
    int64_t& lastCoinStakeSearchInterval
    ): mintingIsRequested_(false)
    , pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , mempool_(transactionMemoryPool)
    , mainCS_(mainCS)
    , masternodeSync_(masternodeSynchronization)
    , mapHashedBlocks_(mapHashedBlocks)
    , lastCoinStakeSearchInterval_(lastCoinStakeSearchInterval)
    , blockFactory_( std::make_shared<BlockFactory>(*pwallet,lastCoinStakeSearchInterval_,mapHashedBlocks_,chain_,chainParameters_, mempool_,mainCS_) )
    , peerNotifier_( std::make_shared<PeerNotificationOfMintService>(peers))
    , subsidyContainer_( std::make_shared<SuperblockSubsidyContainer>(chainParameters_) )
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
    return chain_.Tip()->nHeight >= chainParameters_.LAST_POW_BLOCK();
}

bool CoinMinter::satisfiesMintingRequirements() const
{
    bool stakingRequirementsAreMet =
        !(
            chain_.Tip()->nTime < 1471482000 ||
            !peerNotifier_->havePeersToNotify() ||
            pwallet_->IsLocked() ||
            pwallet_->GetStakingBalance() <= 0 ||
            !masternodeSync_.IsSynced()
        );
    if(!stakingRequirementsAreMet) lastCoinStakeSearchInterval_ = 0;
    return stakingRequirementsAreMet;
}
bool CoinMinter::limitStakingSpeed() const
{
    if (mapHashedBlocks_.count(chain_.Tip()->nHeight)) //search our map of hashed blocks, see if bestblock has been hashed yet
    {
        if (GetTime() - mapHashedBlocks_[chain_.Tip()->nHeight] < static_cast<int64_t>(nHashDrift)/2 )
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
    LogPrintf("%s\n", block->ToString());
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

void CoinMinter::IncrementExtraNonce(CBlock* block, CBlockIndex* pindexPrev, unsigned int& nExtraNonce) const
{
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
        coinbaseTx.vout[0].nValue = subsidyContainer_->blockSubsidiesProvider().GetBlockSubsidity(nHeight).nStakeReward;
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
    CBlockIndex* pindexPrev = chain_.Tip();
    if (!pindexPrev)
        return false;

    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey, fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    CBlock* block = &pblocktemplate->block;
    SetCoinbaseRewardAndHeight(*pblocktemplate, fProofOfStake);
    SetBlockHeaders(*pblocktemplate, fProofOfStake);
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    //Stake miner main
    LogPrintf("CPUMiner : proof-of-stake block found %s \n", block->GetHash().ToString().c_str());

    if (!block->SignBlock(*pwallet_)) {
        LogPrintf("BitcoinMiner(): Signing new block failed \n");
        return false;
    }

    LogPrintf("CPUMiner : proof-of-stake block was signed %s \n", block->GetHash().ToString().c_str());
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
    CBlockIndex* pindexPrev = chain_.Tip();
    if (!pindexPrev)
        return false;

    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey, fProofOfStake));

    if (!pblocktemplate.get())
        return false;

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
                LogPrintf("BitcoinMiner:\n");
                LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                blockSuccessfullyCreated = ProcessBlockFound(block, reserveKey);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);

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
    CReserveKey& reserveKey,
    bool fProofOfStake) const
{
    if(fProofOfStake)
        return createProofOfStakeBlock(nExtraNonce, reserveKey);

    return createProofOfWorkBlock(nExtraNonce, reserveKey);
}
