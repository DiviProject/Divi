#include "ChainstateManager.h"

#include <blockmap.h>
#include <chain.h>
#include <coins.h>
#include <sync.h>
#include <txdb.h>
#include <ui_interface.h>

namespace
{

/** The singleton instance or null if none exists.  */
ChainstateManager* instance = nullptr;

/** Lock for accessing the instance global.  */
CCriticalSection instanceLock;

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
  CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
  bool GetCoins(const uint256& txid, CCoins& coins) const override;
  // Writes do not need special protection, as failure to write is handled by
  // the caller.
};

bool CCoinsViewErrorCatcher::GetCoins (const uint256& txid, CCoins& coins) const
{
  try {
    return CCoinsViewBacked::GetCoins(txid, coins);
  } catch (const std::runtime_error& e) {
    uiInterface.ThreadSafeMessageBox(translate("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
    LogPrintf("Error reading from database: %s\n", e.what());
    // Starting the shutdown sequence and returning false to the caller would be
    // interpreted as 'entry not found' (as opposed to unable to read data), and
    // could lead to invalid interpration. Just exit immediately, as we can't
    // continue anyway, and all writes should be atomic.
    abort();
  }
}

} // anonymous namespace

ChainstateManager::ChainstateManager (const size_t blockTreeCache, const size_t coinDbCache,
                                      const bool fMemory, const bool fWipe)
  : blockMap(new BlockMap ()),
    activeChain(new CChain ()),
    blockTree(new CBlockTreeDB (blockTreeCache, fMemory, fWipe)),
    coinsDbView(new CCoinsViewDB (*blockMap, coinDbCache, fMemory, fWipe)),
    coinsCatcher(new CCoinsViewErrorCatcher (coinsDbView.get ())),
    coinsTip(new CCoinsViewCache (coinsCatcher.get ()))
{
  LOCK (instanceLock);
  assert (instance == nullptr);
  instance = this;
}

ChainstateManager::~ChainstateManager ()
{
  LOCK (instanceLock);
  assert (instance == this);
  instance = nullptr;

  coinsTip.reset ();
  coinsCatcher.reset ();
  coinsDbView.reset ();

  blockTree.reset ();
  activeChain.reset ();
  blockMap.reset ();
}

ChainstateManager& ChainstateManager::Get ()
{
  LOCK (instanceLock);
  assert (instance != nullptr);
  return *instance;
}
