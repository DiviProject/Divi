#include <TransactionDiskAccessor.h>

#include <BlockDiskAccessor.h>
#include <sync.h>
#include <txmempool.h>
#include <coins.h>
#include <chain.h>
#include <txdb.h>
#include <BlockFileOpener.h>
#include <clientversion.h>
#include <Logging.h>

extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern CBlockTreeDB* pblocktree;
extern bool fTxIndex;
extern CCoinsViewCache* pcoinsTip;
extern CChain chainActive;

/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256& hash, CTransaction& txOut, uint256& hashBlock, bool fAllowSlow)
{
    CBlockIndex* pindexSlow = NULL;
    {
        LOCK(cs_main);
        {
            if (mempool.lookup(hash, txOut)) {
                return true;
            }
        }

        if (fTxIndex) {
            CDiskTxPos postx;
            if (pblocktree->ReadTxIndex(hash, postx)) {
                CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                if (file.IsNull())
                    return error("%s: OpenBlockFile failed", __func__);
                CBlockHeader header;
                try {
                    file >> header;
                    fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                    file >> txOut;
                } catch (std::exception& e) {
                    return error("%s : Deserialize or I/O error - %s", __func__, e.what());
                }
                hashBlock = header.GetHash();
                if (txOut.GetHash() != hash)
                    return error("%s : txid mismatch", __func__);
                return true;
            }

            // transaction not found in the index, nothing more can be done
            return false;
        }

        if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
            int nHeight = -1;
            {
                CCoinsViewCache& view = *pcoinsTip;
                const CCoins* coins = view.AccessCoins(hash);
                if (coins)
                    nHeight = coins->nHeight;
            }
            if (nHeight > 0)
                pindexSlow = chainActive[nHeight];
        }
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow)) {
            BOOST_FOREACH (const CTransaction& tx, block.vtx) {
                if (tx.GetHash() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}

bool CollateralIsExpectedAmount(const COutPoint &outpoint, int64_t expectedAmount)
{
    CCoins coins;
    LOCK(cs_main);
    if (!pcoinsTip->GetCoins(outpoint.hash, coins))
        return false;

    int n = outpoint.n;
    if (n < 0 || (unsigned int)n >= coins.vout.size() || coins.vout[n].IsNull()) {
        return false;
    }
    else if (coins.vout[n].nValue != expectedAmount)
    {
        return false;
    }
    else {
        return true;
    }
    assert(false);
}