#include <FlushChainState.h>

#include <ChainstateManager.h>
#include <coins.h>
#include <utiltime.h>
#include <BlockFileHelpers.h>
#include <NotificationInterface.h>
#include <BlockDiskAccessor.h>
#include <txdb.h>
#include <sync.h>
#include <ValidationState.h>
#include <chain.h>
#include <defaultValues.h>

bool FlushStateToDisk(
    CValidationState& state,
    FlushStateMode mode,
    MainNotificationSignals& mainNotificationSignals,
    CCriticalSection& mainCriticalSection)
{
    LOCK(mainCriticalSection);

    ChainstateManager::Reference chainstate;
    auto& coinsTip = chainstate->CoinsTip();
    auto& blockTreeDB = chainstate->BlockTree();

    static int64_t nLastWrite = 0;
    try {
        if ((mode == FLUSH_STATE_ALWAYS) ||
            ((mode == FLUSH_STATE_PERIODIC || mode == FLUSH_STATE_IF_NEEDED) && coinsTip.GetCacheSize() > chainstate->GetNominalViewCacheSize() ) ||
            (mode == FLUSH_STATE_PERIODIC && GetTimeMicros() > nLastWrite + DATABASE_WRITE_INTERVAL * 1000000))
        {
            // Typical CCoins structures on disk are around 100 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace(100 * 2 * 2 * coinsTip.GetCacheSize()))
            {
                return state.Abort("Disk space is low!");
            }
            // First make sure all block and undo data is flushed to disk.
            // Then update all block file information (which may refer to block and undo files).
            if(!BlockFileHelpers::WriteBlockFileToBlockTreeDatabase(state,blockTreeDB))
            {
                return false;
            }
            blockTreeDB.Sync();
            // Finally flush the chainstate (which may refer to block index entries).
            if (!coinsTip.Flush())
                return state.Abort("Failed to write to coin database");
            // Update best block in wallet (so we can detect restored wallets).
            if (mode != FLUSH_STATE_IF_NEEDED) {
                mainNotificationSignals.SetBestChain(chainstate->ActiveChain().GetLocator());
            }
            nLastWrite = GetTimeMicros();
        }
    } catch (const std::runtime_error& e) {
        return state.Abort(std::string("System error while flushing: ") + e.what());
    }
    return true;
}