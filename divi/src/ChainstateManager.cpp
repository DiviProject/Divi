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

class CCoinsViewErrorCatcher final: public CCoinsView
{
private:
    CCoinsViewBacked backingView_;
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : backingView_(view) {}
    bool GetCoins(const uint256& txid, CCoins& coins) const override;
    bool HaveCoins(const uint256& txid) const override;
    uint256 GetBestBlock() const override;
    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock) override;
    // Writes do not need similar protection, as failure to write is handled by the caller.
};
bool CCoinsViewErrorCatcher::HaveCoins(const uint256& txid) const
{
    return backingView_.HaveCoins(txid);
}
uint256 CCoinsViewErrorCatcher::GetBestBlock() const
{
    return backingView_.GetBestBlock();
}
bool CCoinsViewErrorCatcher::BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock)
{
    return backingView_.BatchWrite(mapCoins,hashBlock);
}
bool CCoinsViewErrorCatcher::GetCoins(const uint256& txid, CCoins& coins) const
{
    try {
        return backingView_.GetCoins(txid, coins);
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

ChainstateManager::ChainstateManager (const size_t blockTreeCache, const size_t coinDbCache,size_t viewCacheSize,
                                      const bool fMemory, const bool fWipe)
  : blockMap(new BlockMap ()),
    activeChain(new CChain ()),
    blockTree(new CBlockTreeDB (blockTreeCache, fMemory, fWipe)),
    coinsDbView(new CCoinsViewDB (*blockMap, coinDbCache, fMemory, fWipe)),
    coinsCatcher(new CCoinsViewErrorCatcher (coinsDbView.get ())),
    coinsTip(new CCoinsViewCache (coinsCatcher.get ())),
    viewCacheSize_(viewCacheSize),
    refs(0)
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

  assert (refs == 0);

  coinsTip.reset ();
  coinsCatcher.reset ();
  coinsDbView.reset ();

  blockTree->WriteFlag("shutdown", true);
  blockTree.reset ();
  activeChain.reset ();
  blockMap.reset ();
}

const CCoinsViewDB& ChainstateManager::GetNonCatchingCoinsView () const
{
  return *coinsDbView;
}

ChainstateManager& ChainstateManager::Get ()
{
  LOCK (instanceLock);
  assert (instance != nullptr);
  return *instance;
}

ChainstateManager::Reference::Reference ()
  : instance(ChainstateManager::Get ())
{
  ++instance.refs;
}

ChainstateManager::Reference::~Reference ()
{
  const int prev = instance.refs.fetch_sub (1);
  assert (prev > 0);
}
