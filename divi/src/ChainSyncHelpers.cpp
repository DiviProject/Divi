#include <ChainSyncHelpers.h>

#include <chain.h>
#include <BlockCheckingHelpers.h>
#include <ChainstateManager.h>
#include <sync.h>
#include <utiltime.h>
#include <Settings.h>

const CBlockIndex* pindexBestHeader = nullptr;

void InitializeBestHeaderBlockIndex()
{
    if(pindexBestHeader == nullptr)
        pindexBestHeader = ChainstateManager::Reference()->ActiveChain().Tip();
}
void updateBestHeaderBlockIndex(const CBlockIndex* otherBlockIndex, bool compareByWorkOnly)
{
    if(pindexBestHeader == nullptr || ( otherBlockIndex != nullptr && CBlockIndexWorkComparator(compareByWorkOnly)(pindexBestHeader,otherBlockIndex)))
    {
        pindexBestHeader = otherBlockIndex;
    }
}
int GetBestHeaderBlockHeight()
{
    return pindexBestHeader? pindexBestHeader->nHeight: -1;
}
int64_t GetBestHeaderBlocktime()
{
    return pindexBestHeader? pindexBestHeader->GetBlockTime(): 0;
}

bool IsInitialBlockDownload(CCriticalSection& mainCriticalSection, const Settings& settings)
{
    LOCK(mainCriticalSection);

    const ChainstateManager::Reference chainstate;
    const int64_t height = chainstate->ActiveChain().Height();

    if (settings.isImportingFiles() || settings.isReindexingBlocks() || settings.isStartupVerifyingBlocks())
        return true;
    static bool lockIBDState = false;
    if (lockIBDState)
        return false;
    bool state = (height < GetBestHeaderBlockHeight() - 24 * 6 ||
                  GetBestHeaderBlocktime() < GetTime() - 6 * 60 * 60); // ~144 blocks behind -> 2 x fork detection time
    if (!state)
        lockIBDState = true;
    return state;
}
