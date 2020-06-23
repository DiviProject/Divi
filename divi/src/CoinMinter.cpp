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

CoinMinter::CoinMinter(
    CWallet* pwallet,
    CChain& chain,
    const CChainParams& chainParameters,
    std::vector<CNode*>& peers,
    CMasternodeSync& masternodeSynchronization,
    HashedBlockMap& mapHashedBlocks
    ): pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , peerNotifier_(new PeerNotificationOfMintService(peers))
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
    return chain_.Tip()->nHeight >= chainParameters_.LAST_POW_BLOCK();
}
const int64_t& CoinMinter::getTimeTillNextCheck() const
{
    return timeToWait_;
}

bool CoinMinter::satisfiesMintingRequirements() const
{
    bool stakingRequirements =
        !(chain_.Tip()->nTime < 1471482000 || 
        vNodes.empty() || 
        pwallet_->IsLocked() || 
        nReserveBalance >= pwallet_->GetBalance() || 
        !masternodeSync_.IsSynced());
    if(!stakingRequirements) nLastCoinStakeSearchInterval = 0;
    return stakingRequirements;
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

bool CoinMinter::isMintable()
{
    if(!hasMintableCoinForProofOfStake()) return false;
    
    return true;
}

void CoinMinter::sleep(uint64_t milliseconds) const
{
    MilliSleep(milliseconds);
}

// Actual mining functions
void SetRequiredWork(CBlock& block)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    block.nBits = GetNextWorkRequired(pindexPrev, &block,Params());
}

int64_t SetBlockTime(CBlock& block)
{
    block.nTime = GetAdjustedTime();
    int64_t nSearchTime = block.nTime; // search to current time
    return nSearchTime;
}

void AddTransactionToBlockAndSetDefaultFees(CBlock& block, std::unique_ptr<CBlockTemplate>& pblocktemplate, const CMutableTransaction& txNew)
{
    block.vtx.push_back(txNew);
    pblocktemplate->vTxFees.push_back(-1);   // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end
}

void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev)
{
    block->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    // Updating time can change work required on testnet:
    if (Params().AllowMinDifficultyBlocks())
        block->nBits = GetNextWorkRequired(pindexPrev, block,Params());
}

bool ProcessBlockFound(CBlock* block, CWallet& wallet, CReserveKey& reservekey)
{
    LogPrintf("%s\n", block->ToString());
    LogPrintf("generated %s\n", FormatMoney(block->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (block->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("DIVIMiner : generated block is stale");
    }

    // Remove key from key pool
    reservekey.KeepKey();

    // Track how many getdata requests this block gets
    {
        LOCK(wallet.cs_wallet);
        wallet.mapRequestCount[block->GetHash()] = 0;
    }

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, NULL, block))
        return error("DIVIMiner : ProcessNewBlock, block not accepted");

    for (CNode* node : vNodes) {
        node->PushInventory(CInv(MSG_BLOCK, block->GetHash()));
    }

    return true;
}

void IncrementExtraNonce(CBlock* block, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
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

CMutableTransaction CreateCoinbaseTransaction(const CScript& scriptPubKeyIn)
{
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;
    return txNew;
}

extern int64_t nLastCoinStakeSearchInterval;
bool CreateAndFindStake(
    int64_t nSearchTime, 
    int64_t& nLastCoinStakeSearchTime, 
    CWallet& pwallet, 
    CBlock& block, 
    CMutableTransaction& txCoinStake)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        unsigned int nTxNewTime = 0;
        if (pwallet.CreateCoinStake(pwallet, block.nBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime)) {
            block.nTime = nTxNewTime;
            block.vtx[0].vout[0].SetEmpty();
            block.vtx.push_back(CTransaction(txCoinStake));
            fStakeFound = true;
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}

CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake)
{
    CReserveKey reservekey(pwallet);

    // Create new block
    unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get())
        return NULL;
    CBlock* block = &pblocktemplate->block; // pointer for convenience

    // Create coinbase tx
    CMutableTransaction txNew = CreateCoinbaseTransaction(scriptPubKeyIn);
    
    AddTransactionToBlockAndSetDefaultFees(*block, pblocktemplate, txNew);

    // ppcoin: if coinstake available add coinstake tx
    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // only initialized at startup

    if (fProofOfStake) {
        boost::this_thread::interruption_point();
        
        CMutableTransaction txCoinStake;

        SetRequiredWork(*block);

        if (!CreateAndFindStake(SetBlockTime(*block), nLastCoinStakeSearchTime, *pwallet, *block, txCoinStake))
            return NULL;
    }

    // Collect memory pool transactions into the block

    if(!BlockMemoryPoolTransactionCollector (mempool,cs_main)
        .CollectTransactionsIntoBlock(
            pblocktemplate,
            fProofOfStake,
            txNew
        ))
    {
        return NULL;
    }

    LogPrintf("CreateNewBlock(): releasing template %s\n", "");
    return pblocktemplate.release();
}

CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake)
{
    CPubKey pubkey;
    if (!reservekey.GetReservedKey(pubkey, false))
        return NULL;

    CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    return CreateNewBlock(scriptPubKey, pwallet, fProofOfStake);
}

bool CoinMinter::createNewBlock(
    unsigned int nExtraNonce, 
    CReserveKey& reserveKey, 
    bool fProofOfStake) const
{
    unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev)
        return false;

    unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reserveKey, pwallet_, fProofOfStake));

    if (!pblocktemplate.get())
        return false;

    CBlock* block = &pblocktemplate->block;
    IncrementExtraNonce(block, pindexPrev, nExtraNonce);

    //Stake miner main
    if (fProofOfStake) {
        LogPrintf("CPUMiner : proof-of-stake block found %s \n", block->GetHash().ToString().c_str());

        if (!block->SignBlock(*pwallet_)) {
            LogPrintf("BitcoinMiner(): Signing new block failed \n");
            return false;
        }

        LogPrintf("CPUMiner : proof-of-stake block was signed %s \n", block->GetHash().ToString().c_str());
        SetThreadPriority(THREAD_PRIORITY_NORMAL);
        ProcessBlockFound(block, *pwallet_, reserveKey);
        SetThreadPriority(THREAD_PRIORITY_LOWEST);

        return false;
    }

    LogPrintf("Running DIVIMiner with %u transactions in block (%u bytes)\n", block->vtx.size(),
                ::GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION));

    //
    // Search
    //
    int64_t nStart = GetTime();
    uint256 hashTarget = uint256().SetCompact(block->nBits);
    while (true) 
    {
        unsigned int nHashesDone = 0;

        uint256 hash;
        while (true) {
            hash = block->GetHash();
            if (hash <= hashTarget) {
                // Found a solution
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                LogPrintf("BitcoinMiner:\n");
                LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                ProcessBlockFound(block, *pwallet_, reserveKey);
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
        if (vNodes.empty() && chainParameters_.MiningRequiresPeers())
            break;
        if (block->nNonce >= 0xffff0000)
            break;
        if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
            break;
        if (pindexPrev != chainActive.Tip())
            break;

        // Update nTime every few seconds
        UpdateTime(block, pindexPrev);
        if (chainParameters_.AllowMinDifficultyBlocks()) {
            // Changing block->nTime can change work required on testnet:
            hashTarget.SetCompact(block->nBits);
        }
    }
    return true;
}