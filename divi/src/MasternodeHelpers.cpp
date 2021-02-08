#include <MasternodeHelpers.h>

#include <sync.h>
#include <chain.h>
#include <utiltime.h>

extern CCriticalSection cs_main;
extern bool fImporting;
extern bool fReindex;
extern CChain chainActive;
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
