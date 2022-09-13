#include <BlockCheckingHelpers.h>


#include <primitives/block.h>
#include <chainparams.h>
#include <Logging.h>
#include <ValidationState.h>
#include <Settings.h>
#include <utiltime.h>
#include <defaultValues.h>
#include <version.h>
#include <TransactionOpCounting.h>
#include <utilmoneystr.h>
#include <timedata.h>
#include <sync.h>
#include <chain.h>
#include <map>
#include <ChainstateManager.h>
#include <blockmap.h>

extern Settings& settings;
const CBlockIndex* mostWorkInvalidBlockIndex = nullptr;

bool CheckBlock(const CBlock& block, CValidationState& state)
{
    // These are checks that are independent of context.

    // Check timestamp
    LogPrint("debug", "%s: block=%s  is proof of stake=%d\n", __func__, block.GetHash(), block.IsProofOfStake());
    if (block.GetBlockTime() > GetAdjustedTime() + (block.IsProofOfStake() ? settings.MaxFutureBlockDrift() : 7200)) // 3 minute future drift for PoS
        return state.Invalid(error("%s : block timestamp too far in the future",__func__),
                             REJECT_INVALID, "time-too-new");

    // Check the merkle root.
    bool mutated;
    uint256 hashMerkleRoot2 = block.BuildMerkleTree(&mutated);
    if (block.hashMerkleRoot != hashMerkleRoot2)
        return state.DoS(100, error("%s : hashMerkleRoot mismatch",__func__),
                         REJECT_INVALID, "bad-txnmrklroot", true);

    // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
    // of transactions in a block without affecting the merkle root of a block,
    // while still invalidating it.
    if (mutated)
        return state.DoS(100, error("%s : duplicate transaction",__func__),
                         REJECT_INVALID, "bad-txns-duplicate", true);

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE_CURRENT || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE_CURRENT)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("%s : first tx is not coinbase",__func__),
                         REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("%s : more than one coinbase",__func__),
                             REJECT_INVALID, "bad-cb-multiple");

    if (block.IsProofOfStake()) {
        // Coinbase output should be empty if proof-of-stake block
        if (block.vtx[0].vout.size() != 1 || !block.vtx[0].vout[0].IsEmpty())
            return state.DoS(100, error("%s : coinbase output not empty for proof-of-stake block",__func__));

        // Second transaction must be coinstake, the rest must not be
        if (block.vtx.empty() || !block.vtx[1].IsCoinStake())
            return state.DoS(100, error("%s : second tx is not coinstake",__func__));
        for (unsigned int i = 2; i < block.vtx.size(); i++)
            if (block.vtx[i].IsCoinStake())
                return state.DoS(100, error("%s : more than one coinstake",__func__));
    }

    // Check transactions
    std::set<COutPoint> inputsUsedByBlockTransactions;
    for (const CTransaction& tx : block.vtx) {
        if (!CheckTransaction(tx, state,inputsUsedByBlockTransactions))
            return error("%s : CheckTransaction failed",__func__);
    }


    unsigned int nSigOps = 0;
    for(const CTransaction& tx: block.vtx) {
        nSigOps += GetLegacySigOpCount(tx);
    }
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_LEGACY;
    if (nSigOps > nMaxBlockSigOps)
        return state.DoS(100, error("%s : out-of-bounds SigOpCount",__func__),
                         REJECT_INVALID, "bad-blk-sigops", true);

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState& state, std::set<COutPoint>& usedInputsSet)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, error("%s : vin empty",__func__),
                         REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, error("%s : vout empty",__func__),
                         REJECT_INVALID, "bad-txns-vout-empty");

    // Size limits
    unsigned int nMaxSize = MAX_STANDARD_TX_SIZE;

    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > nMaxSize)
        return state.DoS(100, error("%s : size limits failed",__func__),
                         REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    const CAmount maxMoneyAllowedInOutput = Params().MaxMoneyOut();
    for(const CTxOut& txout: tx.vout)
    {
        if (txout.IsEmpty() && !tx.IsCoinBase() && !tx.IsCoinStake())
            return state.DoS(100, error("%s: txout empty for user transaction",__func__));
        if(!MoneyRange(txout.nValue,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout.nValue out of range",__func__),
                             REJECT_INVALID, "bad-txns-vout-negative-or-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut,maxMoneyAllowedInOutput))
            return state.DoS(100, error("%s : txout total out of range",__func__),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs
    for (const CTxIn& txin : tx.vin) {
        if (usedInputsSet.count(txin.prevout))
            return state.DoS(100, error("%s : duplicate inputs",__func__),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");

        usedInputsSet.insert(txin.prevout);
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 150)
            return state.DoS(100, error("%s : coinbase script size=%d",__func__, tx.vin[0].scriptSig.size()),
                    REJECT_INVALID, "bad-cb-length");
    }

    return true;
}
bool CheckTransaction(const CTransaction& tx, CValidationState& state)
{
    std::set<COutPoint> vInOutPoints;
    return CheckTransaction(tx,state,vInOutPoints);
}


void VerifyBlockIndexTree(
    const ChainstateManager& chainstate,
    CCriticalSection& mainCriticalSection,
    BlockIndexSuccessorsByPreviousBlockIndex& blockSuccessorsByPrevBlockIndex,
    BlockIndexCandidates& blockIndexCandidates)
{
    if (!settings.GetBoolArg("-checkblockindex", Params().DefaultConsistencyChecks())) {
        return;
    }

    LOCK(mainCriticalSection);

    const auto& blockMap = chainstate.GetBlockMap();
    const auto& chain = chainstate.ActiveChain();

    // During a reindex, we read the genesis block and call VerifyBlockIndexTree before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chain.Height() < 0) {
        assert(blockMap.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    BlockIndexSuccessorsByPreviousBlockIndex forward;
    for (const auto& entry : blockMap) {
        forward.insert(std::make_pair(entry.second->pprev, entry.second));
    }

    assert(forward.size() == blockMap.size());

    std::pair<BlockIndexSuccessorsByPreviousBlockIndex::iterator, BlockIndexSuccessorsByPreviousBlockIndex::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex* pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL;         // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL;         // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNotTreeValid = NULL;    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL;   // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == Params().HashGenesisBlock()); // Genesis block's hash must match.
            assert(pindex == chain.Genesis());                       // The current active chain's genesis block must be this block.
        }
        // HAVE_DATA is equivalent to VALID_TRANSACTIONS and equivalent to nTx > 0 (we stored the number of transactions in the block)
        assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0));
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0); // nSequenceId can't be set for blocks that aren't linked
        // All parents having data is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstMissing != NULL) == (pindex->nChainTx == 0));                                             // nChainTx == 0 is used to signal that all parent block's transaction data is available.
        assert(pindex->nHeight == nHeight);                                                                          // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork);                            // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));                                // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL);                                                                     // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL);       // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL);     // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chain.Tip()) && pindexFirstMissing == NULL) {
            if (pindexFirstInvalid == NULL) { // If this block sorts at least as good as the current tip and is valid, it must be in blockIndexCandidates.
                assert(blockIndexCandidates.count(pindex));
            }
        } else { // If this block sorts worse than the current tip, it cannot be in blockIndexCandidates.
            assert(blockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in blockSuccessorsByPrevBlockIndex.
        std::pair<BlockIndexSuccessorsByPreviousBlockIndex::iterator, BlockIndexSuccessorsByPreviousBlockIndex::iterator> rangeUnlinked = blockSuccessorsByPrevBlockIndex.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && pindex->nStatus & BLOCK_HAVE_DATA && pindexFirstMissing != NULL) {
            if (pindexFirstInvalid == NULL) { // If this block has block data available, some parent doesn't, and has no invalid parents, it must be in blockSuccessorsByPrevBlockIndex.
                assert(foundInUnlinked);
            }
        } else { // If this block does not have block data available, or all parents do, it cannot be in blockSuccessorsByPrevBlockIndex.
            assert(!foundInUnlinked);
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<BlockIndexSuccessorsByPreviousBlockIndex::iterator, BlockIndexSuccessorsByPreviousBlockIndex::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<BlockIndexSuccessorsByPreviousBlockIndex::iterator, BlockIndexSuccessorsByPreviousBlockIndex::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

CBlockIndex* FindMostWorkChain(
    const ChainstateManager& chainstate,
    BlockIndexSuccessorsByPreviousBlockIndex& blockSuccessorsByPrevBlockIndex,
    BlockIndexCandidates& blockIndexCandidates)
{
    const auto& chain = chainstate.ActiveChain();
    do {
        CBlockIndex* pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = blockIndexCandidates.rbegin();
            if (it == blockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex* pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chain.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in blockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (mostWorkInvalidBlockIndex == NULL || pindexNew->nChainWork > mostWorkInvalidBlockIndex->nChainWork))
                    mostWorkInvalidBlockIndex = pindexNew;
                CBlockIndex* pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to blockSuccessorsByPrevBlockIndex,
                        // so that if the block arrives in the future we can try adding
                        // to blockIndexCandidates again.
                        blockSuccessorsByPrevBlockIndex.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    blockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                blockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while (true);
}
uint256 getMostWorkForInvalidBlockIndex()
{
    return mostWorkInvalidBlockIndex? mostWorkInvalidBlockIndex->nChainWork: 0;
}
void updateMostWorkInvalidBlockIndex(const CBlockIndex* invalidBlockIndex, bool reconsider)
{
    if(reconsider && invalidBlockIndex == mostWorkInvalidBlockIndex)
    {
        mostWorkInvalidBlockIndex = nullptr;
    }
    else if(!reconsider)
    {
        if(!invalidBlockIndex || invalidBlockIndex->nChainWork > getMostWorkForInvalidBlockIndex())
        {
            mostWorkInvalidBlockIndex = invalidBlockIndex;
        }
    }
}