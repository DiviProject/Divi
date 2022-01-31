// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "random.h"

#include <assert.h>
#include <sstream>
#include <TransactionLocationReference.h>
#include <Logging.h>

CCoins::CCoins(
    ) : fCoinBase(false)
    , fCoinStake(false)
    , vout(0)
    , nHeight(0)
    , nVersion(0)
{
}

CCoins::CCoins(const CTransaction& tx, int nHeightIn)
{
    FromTx(tx, nHeightIn);
}

void CCoins::FromTx(const CTransaction& tx, int nHeightIn)
{
    fCoinBase = tx.IsCoinBase();
    fCoinStake = tx.IsCoinStake();
    vout = tx.vout;
    nHeight = nHeightIn;
    nVersion = tx.nVersion;
    ClearUnspendable();
}

void CCoins::Clear()
{
    fCoinBase = false;
    fCoinStake = false;
    std::vector<CTxOut>().swap(vout);
    nHeight = 0;
    nVersion = 0;
}
void CCoins::Cleanup()
{
    while (vout.size() > 0 && vout.back().IsNull())
        vout.pop_back();
    if (vout.empty())
        std::vector<CTxOut>().swap(vout);
}
void CCoins::ClearUnspendable()
{
    BOOST_FOREACH (CTxOut& txout, vout) {
        if (txout.scriptPubKey.IsUnspendable())
            txout.SetNull();
    }
    Cleanup();
}
void CCoins::swap(CCoins& to)
{
    std::swap(to.fCoinBase, fCoinBase);
    std::swap(to.fCoinStake, fCoinStake);
    to.vout.swap(vout);
    std::swap(to.nHeight, nHeight);
    std::swap(to.nVersion, nVersion);
}
bool CCoins::IsCoinBase() const
{
    return fCoinBase;
}

bool CCoins::IsCoinStake() const
{
    return fCoinStake;
}
//! check whether a particular output is still available
bool CCoins::IsAvailable(unsigned int nPos) const
{
    return (nPos < vout.size() && !vout[nPos].IsNull());
}

//! check whether the entire CCoins is spent
//! note that only !IsPruned() CCoins can be serialized
bool CCoins::IsPruned() const
{
    BOOST_FOREACH (const CTxOut& out, vout)
        if (!out.IsNull())
            return false;
    return true;
}

/**
 * calculate number of bytes for the bitmask, and its number of non-zero bytes
 * each bit in the bitmask represents the availability of one output, but the
 * availabilities of the first two outputs are encoded separately
 */
void CCoins::CalcMaskSize(unsigned int& nBytes, unsigned int& nNonzeroBytes) const
{
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2 + b * 8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2 + b * 8 + i < vout.size(); i++) {
            if (!vout[2 + b * 8 + i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoins::Spend(const int nPos, CTxInUndo& undo)
{
    if (nPos >= vout.size())
        return false;
    if (vout[nPos].IsNull())
        return false;
    undo = CTxInUndo(vout[nPos]);
    vout[nPos].SetNull();
    Cleanup();
    if (vout.size() == 0) {
        undo.nHeight = nHeight;
        undo.fCoinBase = fCoinBase;
        undo.fCoinStake = fCoinStake;
        undo.nVersion = this->nVersion;
    }
    return true;
}

bool CCoins::Spend(const int nPos)
{
    CTxInUndo undo;
    return Spend(nPos, undo);
}

std::string CCoins::ToString() const
{
    std::ostringstream res;
    res << "CCoins(coinbase=" << fCoinBase << ", coinstake=" << fCoinStake;
    res << ", height=" << nHeight << ", version=" << nVersion << "):";
    res << std::endl;

    for (const auto& out : vout)
      res << "  " << out.ToString() << std::endl;

    return res.str();
}

CCoinsViewBacked::CCoinsViewBacked()
  : roBase(nullptr), writeBase(nullptr)
{}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView* viewIn)
  : roBase(viewIn), writeBase(viewIn)
{}

CCoinsViewBacked::CCoinsViewBacked(const CCoinsView* viewIn)
  : roBase(viewIn), writeBase(nullptr)
{}

bool CCoinsViewBacked::GetCoins(const uint256& txid, CCoins& coins) const { return roBase? roBase->GetCoins(txid, coins):false; }
bool CCoinsViewBacked::HaveCoins(const uint256& txid) const { return roBase? roBase->HaveCoins(txid):false; }
uint256 CCoinsViewBacked::GetBestBlock() const { return roBase? roBase->GetBestBlock():uint256(0); }

void CCoinsViewBacked::SetBackend(CCoinsView& viewIn)
{
  roBase = &viewIn;
  writeBase = &viewIn;
}

void CCoinsViewBacked::SetBackend(const CCoinsView& viewIn)
{
  roBase = &viewIn;
  writeBase = nullptr;
}

void CCoinsViewBacked::DettachBackend()
{
  roBase = nullptr;
  writeBase = nullptr;
}

bool CCoinsViewBacked::BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock)
{
  return writeBase? writeBase->BatchWrite(mapCoins, hashBlock):false;
}


CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}

CCoinsViewCache::CCoinsViewCache() : backed_(), hasModifier(false), hashBlock(0), cacheCoins() {}
CCoinsViewCache::CCoinsViewCache(CCoinsView* baseIn) : backed_(baseIn), hasModifier(false), hashBlock(0), cacheCoins() {}
CCoinsViewCache::CCoinsViewCache(const CCoinsView* baseIn) : backed_(baseIn), hasModifier(false), hashBlock(0), cacheCoins() {}

CCoinsViewCache::~CCoinsViewCache()
{
    assert(!hasModifier);
}

CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256& txid) const
{
    CCoinsMap::iterator it = cacheCoins.find(txid);
    if (it != cacheCoins.end())
        return it;
    CCoins tmp;
    if (!backed_.GetCoins(txid, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    return ret;
}

bool CCoinsViewCache::GetCoins(const uint256& txid, CCoins& coins) const
{
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it != cacheCoins.end()) {
        coins = it->second.coins;
        return true;
    }
    return false;
}

CCoinsModifier CCoinsViewCache::ModifyCoins(const uint256& txid)
{
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    if (ret.second) {
        if (!backed_.GetCoins(txid, ret.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            ret.first->second.coins.Clear();
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (ret.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first);
}

const CCoins* CCoinsViewCache::AccessCoins(const uint256& txid) const
{
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256& txid) const
{
    CCoinsMap::const_iterator it = FetchCoins(txid);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

uint256 CCoinsViewCache::GetBestBlock() const
{
    if (hashBlock == uint256(0))
        hashBlock = backed_.GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256& hashBlockIn)
{
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap& coinUpdates, const uint256& hashBlockIn)
{
    assert(!hasModifier);
    for (CCoinsMap::iterator coinUpdate = coinUpdates.begin(); coinUpdate != coinUpdates.end(); coinUpdates.erase(coinUpdate++))
    {
        if (coinUpdate->second.flags & CCoinsCacheEntry::DIRTY)
        { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator matchingCachedCoin = cacheCoins.find(coinUpdate->first);
            const bool coinUpdateIsPruned = coinUpdate->second.coins.IsPruned();
            const bool matchingCoinExistInCache = matchingCachedCoin != cacheCoins.end();
            if (!matchingCoinExistInCache)
            { // Add unknown entry to local cache with not-prunned coin from incoming updates
                if (!coinUpdateIsPruned)
                {
                    assert(coinUpdate->second.flags & CCoinsCacheEntry::FRESH);
                    CCoinsCacheEntry& entry = cacheCoins[coinUpdate->first];
                    entry.coins.swap(coinUpdate->second.coins);
                    entry.flags = CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::FRESH;
                }
            }
            else
            {
                if ((matchingCachedCoin->second.flags & CCoinsCacheEntry::FRESH) && coinUpdateIsPruned)
                {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cacheCoins.erase(matchingCachedCoin);
                }
                else
                {
                    // A normal modification.
                    matchingCachedCoin->second.coins.swap(coinUpdate->second.coins);
                    matchingCachedCoin->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }
        }
    }
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::Flush()
{
    bool fOk = backed_.BatchWrite(cacheCoins, hashBlock);
    cacheCoins.clear();
    return fOk;
}

unsigned int CCoinsViewCache::GetCacheSize() const
{
    return cacheCoins.size();
}

const CTxOut& CCoinsViewCache::GetOutputFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += GetOutputFor(tx.vin[i]).nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            const COutPoint& prevout = tx.vin[i].prevout;
            const CCoins* coins = AccessCoins(prevout.hash);
            if (!coins || !coins->IsAvailable(prevout.n)) {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::ComputeInputCoinAge(const CTransaction& tx, int nHeight) const
{
    if (tx.IsCoinBase() || tx.IsCoinStake())
        return 0.0;
    double dResult = 0.0;
    for (const CTxIn& txin:  tx.vin) {
        const CCoins* coins = AccessCoins(txin.prevout.hash);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight < nHeight) {
            dResult += coins->vout[txin.prevout.n].nValue * (nHeight - coins->nHeight);
        }
    }
    return dResult;
}

void CCoinsViewCache::UpdateWithConfirmedTransaction(const CTransaction& confirmedTx, const int blockHeight, CTxUndo& txundo)
{
    // mark inputs spent
    if (!confirmedTx.IsCoinBase() ) {
        txundo.vprevout.reserve(confirmedTx.vin.size());
        BOOST_FOREACH (const CTxIn& txin, confirmedTx.vin) {
            txundo.vprevout.push_back(CTxInUndo());
            bool ret = ModifyCoins(txin.prevout.hash)->Spend(txin.prevout.n, txundo.vprevout.back());
            assert(ret);
        }
    }

    // add outputs
    ModifyCoins(confirmedTx.GetHash())->FromTx(confirmedTx, blockHeight);
}

static bool RemoveTxOutputsFromCache(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationReference,
    CCoinsViewCache& view)
{
    bool outputsAvailable = true;
    // Check that all outputs are available and match the outputs in the block itself
    // exactly. Note that transactions with only provably unspendable outputs won't
    // have outputs available even in the block itself, so we handle that case
    // specially with outsEmpty.
    CCoins outsEmpty;
    CCoinsModifier outs = view.ModifyCoins(txLocationReference.hash);
    outs->ClearUnspendable();

    CCoins outsBlock(tx, txLocationReference.blockHeight);
    // The CCoins serialization does not serialize negative numbers.
    // No network rules currently depend on the version here, so an inconsistency is harmless
    // but it must be corrected before txout nversion ever influences a network rule.
    if (outsBlock.nVersion < 0)
        outs->nVersion = outsBlock.nVersion;
    if (*outs != outsBlock)
        outputsAvailable = error("DisconnectBlock() : added transaction mismatch? database corrupted");

    // remove outputs
    outs->Clear();
    return outputsAvailable;
}

static void UpdateCoinsForRestoredInputs(
    const COutPoint& out,
    const CTxInUndo& undo,
    CCoinsModifier& coins,
    bool& fClean)
{
    if (undo.nHeight != 0)
    {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("DisconnectBlock() : undo data overwriting existing transaction");
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    }
    else
    {
        if (coins->IsPruned())
            fClean = fClean && error("DisconnectBlock() : undo data adding output to missing transaction");
    }

    if (coins->IsAvailable(out.n))
        fClean = fClean && error("DisconnectBlock() : undo data overwriting existing output");

    if (coins->vout.size() < out.n + 1)
        coins->vout.resize(out.n + 1);

    coins->vout[out.n] = undo.txout;
}

TxReversalStatus CCoinsViewCache::UpdateWithReversedTransaction(const CTransaction& tx, const TransactionLocationReference& txLocationReference, const CTxUndo* txundo)
{
    bool fClean = true;
    fClean = fClean && RemoveTxOutputsFromCache(tx, txLocationReference, *this);
    if(tx.IsCoinBase()) return fClean? TxReversalStatus::OK : TxReversalStatus::CONTINUE_WITH_ERRORS;
    assert(txundo != nullptr);
    if (txundo->vprevout.size() != tx.vin.size())
    {
        error("%s : transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d",__func__, txundo->vprevout.size(), tx.vin.size());
        return fClean?TxReversalStatus::ABORT_NO_OTHER_ERRORS:TxReversalStatus::ABORT_WITH_OTHER_ERRORS;
    }

    for (unsigned int txInputIndex = tx.vin.size(); txInputIndex-- > 0;)
    {
        const COutPoint& out = tx.vin[txInputIndex].prevout;
        const CTxInUndo& undo = txundo->vprevout[txInputIndex];
        CCoinsModifier coins = ModifyCoins(out.hash);
        UpdateCoinsForRestoredInputs(out,undo,coins,fClean);
    }
    return fClean? TxReversalStatus::OK : TxReversalStatus::CONTINUE_WITH_ERRORS;
}

CCoinsModifier::CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_) : cache(cache_), it(it_)
{
    assert(!cache.hasModifier);
    cache.hasModifier = true;
}

CCoinsModifier::~CCoinsModifier()
{
    assert(cache.hasModifier);
    cache.hasModifier = false;
    it->second.coins.Cleanup();
    if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
        cache.cacheCoins.erase(it);
    }
}
