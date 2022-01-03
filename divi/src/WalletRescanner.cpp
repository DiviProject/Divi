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

int WalletRescanner::scanForWalletTransactions(CWallet& wallet, const CBlockIndex* pindexStart, bool fUpdate)
{
    static const CCheckpointServices checkpointsVerifier(GetCurrentChainCheckpoints);

    int ret = 0;
    int64_t nNow = GetTime();

    const CBlockIndex* pindex = pindexStart;
    {
        LOCK2(mainCS_, wallet.cs_wallet);
        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        const int64_t timestampOfFirstKey = wallet.getTimestampOfFistKey();
        while (pindex && timestampOfFirstKey && (pindex->GetBlockTime() < (timestampOfFirstKey - 7200)))
            pindex = activeChain_.Next(pindex);

        wallet.ShowProgress(translate("Rescanning..."), 0); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        double dProgressStart = checkpointsVerifier.GuessVerificationProgress(pindex, false);
        double dProgressTip = checkpointsVerifier.GuessVerificationProgress(activeChain_.Tip(), false);
        while (pindex) {
            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
                wallet.ShowProgress(translate("Rescanning..."), std::max(1, std::min(99, (int)((checkpointsVerifier.GuessVerificationProgress(pindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100))));

            CBlock block;
            blockReader_.ReadBlock(pindex,block);
            BOOST_FOREACH (CTransaction& tx, block.vtx)
            {
                if (wallet.AddToWalletIfInvolvingMe(tx, &block, fUpdate,TransactionSyncType::RESCAN))
                    ret++;
            }
            pindex = activeChain_.Next(pindex);
            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, checkpointsVerifier.GuessVerificationProgress(pindex));
            }
        }
        wallet.ShowProgress(translate("Rescanning..."), 100); // hide progress dialog in GUI
    }
    return ret;
}
