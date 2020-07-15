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
#include <CoinstakeCreator.h>
#include <boost/thread/thread.hpp>
#include <SuperblockHelpers.h>

#include <BlockFactory.h>


int64_t nLastCoinStakeSearchInterval = 0;

CoinMinter::CoinMinter(
    CWallet* pwallet,
    CChain& chain,
    const CChainParams& chainParameters,
    std::vector<CNode*>& peers,
    CMasternodeSync& masternodeSynchronization,
    HashedBlockMap& mapHashedBlocks,
    CTxMemPool& transactionMemoryPool, 
    AnnotatedMixin<boost::recursive_mutex>& mainCS
    ): mintingIsRequested_(false)
    , pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , mempool_(transactionMemoryPool)
    , mainCS_(mainCS)
    , blockFactory_( std::make_shared<BlockFactory>(*pwallet,nLastCoinStakeSearchInterval,chain_,chainParameters_, mempool_,mainCS_) )
    , peerNotifier_( std::make_shared<PeerNotificationOfMintService>(peers))
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
bool CoinMinter::isAtProofOfStakeHeight() const
{
    return chain_.Tip()->nHeight > chainParameters_.LAST_POW_BLOCK();
}

bool CoinMinter::satisfiesMintingRequirements() const
{
    bool stakingRequirementsAreMet =
        !(
            chain_.Tip()->nTime < 1471482000 ||
            !peerNotifier_->havePeersToNotify() ||
            pwallet_->IsLocked() ||
            nReserveBalance >= pwallet_->GetBalance() ||
            !masternodeSync_.IsSynced()
        );
    if(!stakingRequirementsAreMet) nLastCoinStakeSearchInterval = 0;
    return stakingRequirementsAreMet;
}
bool CoinMinter::limitStakingSpeed() const
{
    if (mapHashedBlocks_.count(chain_.Tip()->nHeight)) //search our map of hashed blocks, see if bestblock has been hashed yet
    {
        if (GetTime() - mapHashedBlocks_[chain_.Tip()->nHeight] < std::max(pwallet_->nHashInterval, (unsigned int)1)) // wait half of the nHashDrift with max wait of 3 minutes
        {
            return true;
        }
    }
    return false;
}

bool CoinMinter::CanMintCoins()
{
    if( !hasMintableCoinForProofOfStake() || 
        !isAtProofOfStakeHeight() ||
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
        if (block->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("DIVIMiner : generated block is stale");
    }

    // Remove key from key pool
    reservekey.KeepKey();

    // Track how many getdata requests this block gets
    {
        LOCK(pwallet_->cs_wallet);
        pwallet_->mapRequestCount[block->GetHash()] = 0;
    }

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
    std::unique_ptr<CBlockTemplate>& pblocktemplate,
    const bool& fProofOfStake) const
{
    SuperblockSubsidyContainer subsidiesContainer(chainParameters_);
    // Compute final coinbase transaction.
    int nHeight = pblocktemplate->previousBlockIndex->nHeight+1;
    CBlock& block = pblocktemplate->block;
    CMutableTransaction& coinbaseTx = *pblocktemplate->coinbaseTransaction;
    block.vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
    if (!fProofOfStake) {
        coinbaseTx.vout[0].nValue = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight).nStakeReward;
        coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
        block.vtx[0] = coinbaseTx;
    }
}

void CoinMinter::SetBlockHeaders(std::unique_ptr<CBlockTemplate>& pblocktemplate, const bool& proofOfStake) const
{
    // Fill in header
    CBlock& block = pblocktemplate->block;
    block.hashPrevBlock = pblocktemplate->previousBlockIndex->GetBlockHash();
    if (!proofOfStake)
        UpdateTime(&block, pblocktemplate->previousBlockIndex);
    block.nBits = GetNextWorkRequired(pblocktemplate->previousBlockIndex, &block, chainParameters_);
    block.nNonce = 0;
    block.nAccumulatorCheckpoint = static_cast<uint256>(0);
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(block.vtx[0]);
}

bool CoinMinter::createProofOfStakeBlock(
    unsigned int nExtraNonce, 
    CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = true;
    bool blockSuccesfullyCreated = false;
    CBlockIndex* pindexPrev = chain_.Tip();
    if (!pindexPrev)
        return false;

    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey,fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    CBlock* block = &pblocktemplate->block;
    SetCoinbaseRewardAndHeight(pblocktemplate, fProofOfStake);
    SetBlockHeaders(pblocktemplate, fProofOfStake);
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    //Stake miner main
    LogPrintf("CPUMiner : proof-of-stake block found %s \n", block->GetHash().ToString().c_str());

    if (!block->SignBlock(*pwallet_)) {
        LogPrintf("BitcoinMiner(): Signing new block failed \n");
        return false;
    }

    LogPrintf("CPUMiner : proof-of-stake block was signed %s \n", block->GetHash().ToString().c_str());
    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    blockSuccesfullyCreated = ProcessBlockFound(block, reserveKey);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    return blockSuccesfullyCreated;
}

bool CoinMinter::createProofOfWorkBlock(
    unsigned int nExtraNonce, 
    CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = false;
    bool blockSuccesfullyCreated = false;
    unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
    CBlockIndex* pindexPrev = chain_.Tip();
    if (!pindexPrev)
        return false;

    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey,fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    CBlock* block = &pblocktemplate->block;
    SetCoinbaseRewardAndHeight(pblocktemplate, fProofOfStake);
    SetBlockHeaders(pblocktemplate, fProofOfStake);
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    LogPrintf("Running DIVIMiner with %u transactions in block (%u bytes)\n", block->vtx.size(),
                ::GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION));

    int64_t nStart = GetTime();
    uint256 hashTarget = uint256().SetCompact(block->nBits);
    while (true) 
    {
        unsigned int nHashesDone = 0;
        blockSuccesfullyCreated = false;
        uint256 hash;
        while (true) {
            hash = block->GetHash();
            if (hash <= hashTarget) 
            {
                // Found a solution
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                LogPrintf("BitcoinMiner:\n");
                LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                blockSuccesfullyCreated = ProcessBlockFound(block, reserveKey);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);

                // In regression test mode, stop mining after a block is found. This
                // allows developers to controllably generate a block on demand.
                if (chainParameters_.MineBlocksOnDemand())
                    throw boost::thread_interrupted();

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
        if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
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
    return blockSuccesfullyCreated;
}


bool CoinMinter::createNewBlock(
    unsigned int nExtraNonce, 
    CReserveKey& reserveKey, 
    bool fProofOfStake) const
{
    if(fProofOfStake)
    {
        return createProofOfStakeBlock(nExtraNonce,reserveKey);
    }
    else
    {
        return createProofOfWorkBlock(nExtraNonce,reserveKey);
    }
}