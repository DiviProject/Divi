#include <ForkWarningHelpers.h>

#include <Warnings.h>
#include <alert.h>
#include <chain.h>
#include <ChainstateManager.h>

#include <BlockCheckingHelpers.h>
#include <pow.h>
#include <Logging.h>
#include <sync.h>

namespace
{

const CBlockIndex* pindexBestForkTip = nullptr;
const CBlockIndex* pindexBestForkBase = nullptr;

} // anonymous namespace

void CheckForkWarningConditions(const Settings& settings, CCriticalSection& mainCriticalSection, bool isInitialBlockDownload)
{
    AssertLockHeld(mainCriticalSection);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (isInitialBlockDownload) return;

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chain.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (getMostWorkForInvalidBlockIndex() > chain.Tip()->nChainWork + (chain.Tip()->getBlockProof() * 6))) {
        if (!Warnings::haveFoundLargeWorkFork() && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                        pindexBestForkBase->phashBlock->ToString() + std::string("'");
                CAlert::Notify(settings,warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                LogPrintf("CheckForkWarningConditions: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n",
                          pindexBestForkBase->nHeight, *pindexBestForkBase->phashBlock,
                          pindexBestForkTip->nHeight, *pindexBestForkTip->phashBlock);
                Warnings::setLargeWorkForkFound(true);
            }
        } else {
            LogPrintf("CheckForkWarningConditions: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n");
            Warnings::setLargeWorkInvalidChainFound(true);
        }
    } else {
        Warnings::setLargeWorkForkFound(false);
        Warnings::setLargeWorkInvalidChainFound(false);
    }
}

void CheckForkWarningConditionsOnNewFork(const Settings& settings, CCriticalSection& mainCriticalSection,const CBlockIndex* pindexNewForkTip, bool isInitialBlockDownload)
{
    AssertLockHeld(mainCriticalSection);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // If we are on a fork that is sufficiently large, set a warning flag
    const CBlockIndex* pfork = pindexNewForkTip;
    const CBlockIndex* plonger = chain.Tip();
    while (pfork && pfork != plonger) {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition which we should warn the user about as a fork of at least 7 blocks
    // who's tip is within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (pfork->getBlockProof() * 7) &&
            chain.Height() - pindexNewForkTip->nHeight < 72) {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions(settings, mainCriticalSection, isInitialBlockDownload);
}