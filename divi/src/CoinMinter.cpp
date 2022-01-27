#include <CoinMinter.h>

#include <I_StakingCoinSelector.h>
#include <utiltime.h>
#include <chain.h>
#include <chainparams.h>
#include <I_PeerBlockNotifyService.h>
#include <masternode-sync.h>
#include <utilmoneystr.h>
#include <timedata.h>
#include <boost/thread.hpp>
#include <I_BlockFactory.h>
#include <BlockTemplate.h>
#include <BlockSigning.h>
#include <I_BlockSubsidyProvider.h>
#include <Logging.h>
#include <MasternodeHelpers.h>
#include <ThreadManagementHelpers.h>
#include <reservekey.h>
#include <script/standard.h>

constexpr int hashingDelay = 45;
extern bool ProcessNewBlockFoundByMe(CBlock* pblock, bool& shouldKeepKey);

CoinMinter::CoinMinter(
    const CChain& chain,
    const CChainParams& chainParameters,
    const I_PeerBlockNotifyService& peerNotifier,
    const CMasternodeSync& masternodeSynchronization,
    I_BlockFactory& blockFactory,
    I_StakingWallet& wallet,
    HashedBlockMap& mapHashedBlocks
    ): chain_(chain)
    , chainParameters_(chainParameters)
    , peerNotifier_( peerNotifier)
    , masternodeSync_(masternodeSynchronization)
    , blockFactory_( blockFactory )
    , wallet_(wallet)
    , mapHashedBlocks_(mapHashedBlocks)
    , mintingIsRequested_(false)
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
        haveMintableCoins_ = wallet_.HasAgedCoins();
    }
    else
    {
        timeToWait_ = fiveMinutes_ - timeWaited;
    }
    return haveMintableCoins_;
}

enum NextBlockType
{
    UNDEFINED_TIP,
    PROOF_OF_STAKE,
    PROOF_OF_WORK,
};

static NextBlockType ComputeNextBlockType(const CBlockIndex* chainTip,const int lastPOWBlock)
{
    return (!chainTip)? UNDEFINED_TIP:
            (chainTip->nHeight < lastPOWBlock)? PROOF_OF_WORK: PROOF_OF_STAKE;
}

bool CoinMinter::nextBlockIsProofOfStake() const
{
    return ComputeNextBlockType(chain_.Tip(), chainParameters_.LAST_POW_BLOCK()) == PROOF_OF_STAKE;
}

bool CoinMinter::satisfiesMintingRequirements() const
{
    const unsigned oneReorgWorthOfTimestampDrift = 60*chainParameters_.MaxReorganizationDepth();
    const unsigned minimumChainTipTimestampForMinting = GetTime() - oneReorgWorthOfTimestampDrift;

    const CBlockIndex* chainTip = chain_.Tip();
    bool chainTipIsSyncedEnough = !(chainTip? chainTip->nTime < minimumChainTipTimestampForMinting: IsBlockchainSynced());
    bool stakingRequirementsAreMet =
        chainTipIsSyncedEnough &&
        peerNotifier_.havePeersToNotify() &&
        wallet_.CanStakeCoins() &&
        masternodeSync_.IsSynced();
    return stakingRequirementsAreMet;
}
bool CoinMinter::limitStakingSpeed() const
{
    const CBlockIndex* chainTip = chain_.Tip();
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

bool CoinMinter::ProcessBlockFound(CBlock* block, CReserveKey& reservekey, const bool isProofOfStake) const
{
    LogPrintf("%s\n", *block);
    LogPrintf("generated %s\n", FormatMoney(block->vtx[0].vout[0].nValue));

    bool shouldKeepKey = false;
    bool successfulBlock = ProcessNewBlockFoundByMe(block,shouldKeepKey);
    if(shouldKeepKey && !isProofOfStake) reservekey.KeepKey();
    if(successfulBlock) peerNotifier_.notifyPeers(block->GetHash());
    return successfulBlock;
}

bool CoinMinter::createProofOfStakeBlock(CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = true;
    bool blockSuccessfullyCreated = false;
    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_.CreateNewPoSBlock());

    if (!pblocktemplate.get())
        return false;

    //Stake miner main
    CBlock* block = &(pblocktemplate->block);
    LogPrintf("%s: proof-of-stake block found %s \n",__func__, block->GetHash());

    if (!SignBlock(wallet_, *block)) {
        LogPrintf("%s: Signing new block failed \n",__func__);
        return false;
    }

    LogPrintf("%s: proof-of-stake block was signed %s \n", __func__, block->GetHash());
    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    blockSuccessfullyCreated = ProcessBlockFound(block, reserveKey,fProofOfStake);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    return blockSuccessfullyCreated;
}

bool CoinMinter::createProofOfWorkBlock(CReserveKey& reserveKey) const
{
    constexpr const bool fProofOfStake = false;
    bool blockSuccessfullyCreated = false;

    CPubKey pubkey;
    if (!fProofOfStake && !reserveKey.GetReservedKey(pubkey, false))
        return NULL;
    CScript scriptPubKey = GetScriptForDestination(pubkey.GetID());
    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_.CreateNewPoWBlock(scriptPubKey));

    if (!pblocktemplate.get())
        return false;

    CBlock* block = &pblocktemplate->block;

    LogPrintf("Running DIVIMiner with %u transactions in block (%u bytes)\n", block->vtx.size(),
                ::GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION));

    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    blockSuccessfullyCreated = ProcessBlockFound(block, reserveKey,fProofOfStake);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    return blockSuccessfullyCreated;
}

bool CoinMinter::createNewBlock() const
{
    if(!mintingIsRequested_) return false;
    CReserveKey reserveKey(wallet_);
    auto status = ComputeNextBlockType(chain_.Tip(), chainParameters_.LAST_POW_BLOCK());
    if(status != UNDEFINED_TIP)
    {
        if(status != PROOF_OF_WORK)
            return createProofOfStakeBlock(reserveKey);

        return createProofOfWorkBlock(reserveKey);
    }
    return false;
}
