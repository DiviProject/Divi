#include <MasternodeHelpers.h>

#include <sync.h>
#include <chain.h>
#include <ChainstateManager.h>
#include <utiltime.h>
#include <timedata.h>
#include <masternode.h>
#include <blockmap.h>
#include <TransactionDiskAccessor.h>
#include <MasternodePing.h>
#include <Logging.h>

extern CCriticalSection cs_main;
extern bool fImporting;
extern bool fReindex;

static bool mnResyncRequested  = false;
bool MasternodeResyncIsRequested()
{
    return mnResyncRequested;
}
void FulfilledMasternodeResyncRequest()
{
    mnResyncRequested = false;
}
static bool IsBlockchainSyncStalled(const int64_t& now)
{
    constexpr int64_t oneHourStalledLimit = 60*60;
    static int64_t lastChecked = GetTime();
    const bool stalled = now - lastChecked > oneHourStalledLimit;
    lastChecked = now;
    return stalled;
}
bool IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    const int64_t now = GetTime();
    if(IsBlockchainSyncStalled(now))
    {
        fBlockchainSynced = false;
        mnResyncRequested = true;
    }

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    const ChainstateManager::Reference chainstate;
    const CBlockIndex* pindex = chainstate->ActiveChain().Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 10 < now)
        return false;

    fBlockchainSynced = true;

    return true;
}


bool GetBlockHashForScoring(uint256& hash, int nBlockHeight)
{
    const ChainstateManager::Reference chainstate;
    const auto* tip = chainstate->ActiveChain().Tip();
    if (tip == nullptr)
        return false;
    return GetBlockHashForScoring(hash, tip, nBlockHeight - tip->nHeight);
}

bool GetBlockHashForScoring(uint256& hash, const CBlockIndex* pindex, const int offset)
{
    if (pindex == nullptr)
        return false;

    const auto* pindexAncestor = pindex->GetAncestor(pindex->nHeight + offset - 101);
    if (pindexAncestor == nullptr)
        return false;

    hash = pindexAncestor->GetBlockHash();
    return true;
}

const CBlockIndex* ComputeCollateralBlockIndex(const CMasternode& masternode)
{
    static std::map<COutPoint,const CBlockIndex*> cachedCollateralBlockIndices;
    const ChainstateManager::Reference chainstate;

    const CBlockIndex* collateralBlockIndex = cachedCollateralBlockIndices[masternode.vin.prevout];
    if (collateralBlockIndex)
    {
        if(chainstate->ActiveChain().Contains(collateralBlockIndex))
        {
            return collateralBlockIndex;
        }
    }

    uint256 hashBlock;
    CTransaction tx;
    if (!GetTransaction(masternode.vin.prevout.hash, tx, hashBlock, true)) {
        collateralBlockIndex = nullptr;
        return collateralBlockIndex;
    }

    const auto& blockMap = chainstate->GetBlockMap();
    const auto mi = blockMap.find(hashBlock);
    if (mi == blockMap.end() || mi->second == nullptr) {
        collateralBlockIndex = nullptr;
        return collateralBlockIndex;
    }

    if (!chainstate->ActiveChain().Contains(mi->second)) {
        collateralBlockIndex = nullptr;
        return collateralBlockIndex;
    }
    collateralBlockIndex = mi->second;
    return collateralBlockIndex;
}

const CBlockIndex* ComputeMasternodeConfirmationBlockIndex(const CMasternode& masternode)
{
    const CBlockIndex* pindexConf = nullptr;
    {
        LOCK(cs_main);
        const ChainstateManager::Reference chainstate;
        const auto* pindexCollateral = ComputeCollateralBlockIndex(masternode);
        if (pindexCollateral == nullptr)
            pindexConf = nullptr;
        else {
            assert(chainstate->ActiveChain().Contains(pindexCollateral));
            pindexConf = chainstate->ActiveChain()[pindexCollateral->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1];
            assert(pindexConf == nullptr || pindexConf->GetAncestor(pindexCollateral->nHeight) == pindexCollateral);
        }
    }
    return pindexConf;
}

int ComputeMasternodeInputAge(const CMasternode& masternode)
{
    LOCK(cs_main);
    const ChainstateManager::Reference chainstate;

    const auto* pindex = ComputeCollateralBlockIndex(masternode);
    if (pindex == nullptr)
        return 0;

    assert(chainstate->ActiveChain().Contains(pindex));

    const unsigned tipHeight = chainstate->ActiveChain().Height();
    assert(tipHeight >= pindex->nHeight);

    return tipHeight - pindex->nHeight + 1;
}

CMasternodePing createCurrentPing(const CTxIn& newVin)
{
    const ChainstateManager::Reference chainstate;
    const auto& activeChain = chainstate->ActiveChain();

    CMasternodePing ping;
    ping.vin = newVin;
    ping.blockHash = activeChain[activeChain.Height() - 12]->GetBlockHash();
    ping.sigTime = GetAdjustedTime();
    ping.signature = std::vector<unsigned char>();
    return ping;
}

bool TimeSinceLastPingIsWithin(const CMasternode& mn, const int timeWindow, int64_t now)
{
    if (now == -1)
        now = GetAdjustedTime();

    if (mn.lastPing == CMasternodePing())
        return false;

    return now - mn.lastPing.sigTime < timeWindow;
}

bool IsTooEarlyToSendPingUpdate(const CMasternode& mn, int64_t now)
{
    return TimeSinceLastPingIsWithin(mn,MASTERNODE_PING_SECONDS, now);
}
bool IsTooEarlyToReceivePingUpdate(const CMasternode& mn, int64_t now)
{
    return TimeSinceLastPingIsWithin(mn, MASTERNODE_MIN_MNP_SECONDS - 60, now);
}
bool ReindexingOrImportingIsActive()
{
    return (fImporting || fReindex);
}
CMasternodePing createDelayedMasternodePing(const CMasternode& mn)
{
    const ChainstateManager::Reference chainstate;
    const auto& activeChain = chainstate->ActiveChain();

    CMasternodePing ping;
    const int64_t offsetTimeBy45BlocksInSeconds = 60 * 45;
    ping.vin = mn.vin;
    const int depthOfTx = ComputeMasternodeInputAge(mn);
    const int offset = std::min( std::max(0, depthOfTx), 12 );
    const auto* block = activeChain[activeChain.Height() - offset];
    ping.blockHash = block->GetBlockHash();
    ping.sigTime = std::max(block->GetBlockTime() + offsetTimeBy45BlocksInSeconds, GetAdjustedTime());
    ping.signature = std::vector<unsigned char>();
    LogPrint("masternode","mnp - relay block-time & sigtime: %d vs. %d\n", block->GetBlockTime(), ping.sigTime);

    return ping;
}
