#include <MasternodeHelpers.h>

#include <sync.h>
#include <chain.h>
#include <utiltime.h>
#include <timedata.h>
#include <masternode.h>
#include <blockmap.h>
#include <TransactionDiskAccessor.h>
#include <MasternodePing.h>

extern CCriticalSection cs_main;
extern bool fImporting;
extern bool fReindex;
extern CChain chainActive;
extern BlockMap mapBlockIndex;

bool IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    const int64_t now = GetTime();
    if (now - lastProcess > 60 * 60) {
        fBlockchainSynced = false;
    }
    lastProcess = now;

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 6000 < now)
        return false;

    fBlockchainSynced = true;

    return true;
}


bool GetBlockHashForScoring(uint256& hash, int nBlockHeight)
{
    const auto* tip = chainActive.Tip();
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

    const CBlockIndex* collateralBlockIndex = cachedCollateralBlockIndices[masternode.vin.prevout];
    if (collateralBlockIndex)
    {
        if(chainActive.Contains(collateralBlockIndex))
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

    const auto mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end() || mi->second == nullptr) {
        collateralBlockIndex = nullptr;
        return collateralBlockIndex;
    }

    if (!chainActive.Contains(mi->second)) {
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
        const auto* pindexCollateral = ComputeCollateralBlockIndex(masternode);
        if (pindexCollateral == nullptr)
            pindexConf = nullptr;
        else {
            assert(chainActive.Contains(pindexCollateral));
            pindexConf = chainActive[pindexCollateral->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1];
            assert(pindexConf == nullptr || pindexConf->GetAncestor(pindexCollateral->nHeight) == pindexCollateral);
        }
    }
    return pindexConf;
}

int ComputeMasternodeInputAge(const CMasternode& masternode)
{
    LOCK(cs_main);

    const auto* pindex = ComputeCollateralBlockIndex(masternode);
    if (pindex == nullptr)
        return 0;

    assert(chainActive.Contains(pindex));

    const unsigned tipHeight = chainActive.Height();
    assert(tipHeight >= pindex->nHeight);

    return tipHeight - pindex->nHeight + 1;
}

CMasternodePing createCurrentPing(const CTxIn& newVin)
{
    CMasternodePing ping;
    ping.vin = newVin;
    ping.blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    ping.sigTime = GetAdjustedTime();
    ping.signature = std::vector<unsigned char>();
    return ping;
}