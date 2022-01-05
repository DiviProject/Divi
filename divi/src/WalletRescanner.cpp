#include <WalletRescanner.h>

#include <I_BlockDataReader.h>
#include <chainparams.h>
#include <chain.h>
#include <wallet.h>
#include <primitives/block.h>
#include <sync.h>
#include <Logging.h>
#include <utiltime.h>
#include <checkpoints.h>

WalletRescanner::WalletRescanner(
    const I_BlockDataReader& blockReader,
    const CChain& activeChain,
    CCriticalSection& mainCS
    ): blockReader_(blockReader)
    , activeChain_(activeChain)
    , mainCS_(mainCS)
{
}

static int computeProgress(int currentHeight,int startHeight,int endHeight)
{
    return std::max(1, std::min(99, (int)((currentHeight - startHeight) / (endHeight - startHeight) * 100)));
}

int WalletRescanner::scanForWalletTransactions(CWallet& wallet, const CBlockIndex* pindexStart)
{
    int ret = 0;
    int64_t nNow = GetTime();

    const CBlockIndex* pindex = pindexStart? pindexStart : activeChain_.Genesis();
    {
        LOCK2(mainCS_, wallet.cs_wallet);
        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        const int64_t timestampOfFirstKey = wallet.getTimestampOfFistKey();
        while (pindex && timestampOfFirstKey && (pindex->GetBlockTime() < (timestampOfFirstKey - 7200)))
            pindex = activeChain_.Next(pindex);

        LogPrintf("Rescanning...%d\n",0);
        const auto endHeight = activeChain_.Tip()->nHeight;
        const auto startHeight = pindex?pindex->nHeight:endHeight;
        while (pindex)
        {
            if (pindex->nHeight % 100 == 0 && endHeight - startHeight > 0)
            {
                const int progress = computeProgress(pindex->nHeight,startHeight,endHeight);
                LogPrintf("Rescanning...%d\n",progress);
            }

            CBlock block;
            blockReader_.ReadBlock(pindex,block);
            BOOST_FOREACH (CTransaction& tx, block.vtx)
            {
                if (wallet.AddToWalletIfInvolvingMe(tx, &block, true,TransactionSyncType::RESCAN))
                    ret++;
            }
            pindex = activeChain_.Next(pindex);
            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%d\n", pindex->nHeight, computeProgress(pindex->nHeight,startHeight,endHeight));
            }
        }
        LogPrintf("Rescanning...%d\n",100);
    }
    return ret;
}
