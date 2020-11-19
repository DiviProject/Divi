#include <merkletx.h>
#include <sync.h>
#include <primitives/block.h>
#include <chainparams.h>
#include <chain.h>
#include <txmempool.h>
#include <ValidationState.h>
#include <swifttx.h>
#include <spork.h>
#include <blockmap.h>
#include <Logging.h>
#include <utiltime.h>

extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;
extern BlockMap mapBlockIndex;
extern CChain chainActive;
extern bool fEnableSwiftTX;
extern int nSwiftTXDepth;

bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee, bool ignoreFees);


CMerkleTx::CMerkleTx(
    const CTransaction& txIn,
    const CChain& activeChain,
    const BlockMap& blockIndices
    ): CTransaction(txIn)
    , activeChain_(activeChain)
    , blockIndices_(blockIndices)
{
}

CMerkleTx::CMerkleTx(): activeChain_(chainActive), blockIndices_(mapBlockIndex)
{
    Init();
}

CMerkleTx::CMerkleTx(const CTransaction& txIn) : CTransaction(txIn), activeChain_(chainActive), blockIndices_(mapBlockIndex)
{
    Init();
}

void CMerkleTx::Init()
{
    hashBlock = 0;
    nIndex = -1;
    fMerkleVerified = false;
}

int CMerkleTx::GetNumberOfBlockConfirmations(bool enableIX) const
{
    const CBlockIndex* pindexRet;
    return GetNumberOfBlockConfirmations(pindexRet, enableIX);
}
bool CMerkleTx::IsInMainChain() const
{
    const CBlockIndex* pindexRet;
    return GetNumberOfBlockConfirmationsINTERNAL(pindexRet) > 0;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    AssertLockHeld(cs_main);
    CBlock blockTmp;

    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)block.vtx.size()) {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return 0;
    }

    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
    if (mi == blockIndices_.end())
        return 0;
    const CBlockIndex* pindex = (*mi).second;
    if (!pindex || !activeChain_.Contains(pindex))
        return 0;

    return activeChain_.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetNumberOfBlockConfirmationsINTERNAL(const CBlockIndex*& pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::const_iterator mi = blockIndices_.find(hashBlock);
    if (mi == blockIndices_.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !activeChain_.Contains(pindex))
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified) {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return activeChain_.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetNumberOfBlockConfirmations(const CBlockIndex*& pindexRet, bool enableIX) const
{
    AssertLockHeld(cs_main);
    int nResult = GetNumberOfBlockConfirmationsINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    if (enableIX) {
        if (nResult < 6) {
            int signatures = GetTransactionLockSignatures();
            if (signatures >= SWIFTTX_SIGNATURES_REQUIRED) {
                return nSwiftTXDepth + nResult;
            }
        }
    }

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return std::max(0, (Params().COINBASE_MATURITY() + 1) - GetNumberOfBlockConfirmations());
}


bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectInsaneFee, bool ignoreFees)
{
    CValidationState state;
    bool fAccepted = ::AcceptToMemoryPool(mempool, state, *this, fLimitFree, NULL, fRejectInsaneFee, ignoreFees);
    if (!fAccepted)
        LogPrintf("%s : %s\n", __func__, state.GetRejectReason());
    return fAccepted;
}

int CMerkleTx::GetTransactionLockSignatures() const
{
    if (fLargeWorkForkFound || fLargeWorkInvalidChainFound) return -2;
    if (!sporkManager.IsSporkActive(SPORK_2_SWIFTTX_ENABLED)) return -3;
    if (!fEnableSwiftTX) return -1;

    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());
    if (i != mapTxLocks.end()) {
        return (*i).second.CountSignatures();
    }

    return -1;
}

bool CMerkleTx::IsTransactionLockTimedOut() const
{
    if (!fEnableSwiftTX) return 0;

    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(GetHash());
    if (i != mapTxLocks.end()) {
        return GetTime() > (*i).second.nTimeout;
    }

    return false;
}