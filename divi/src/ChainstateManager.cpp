#include "ChainstateManager.h"

#include <sync.h>

extern BlockMap mapBlockIndex;
extern CChain chainActive;
extern CBlockTreeDB* pblocktree;
extern CCoinsViewCache* pcoinsTip;

namespace
{

/** The singleton instance or null if none exists.  */
ChainstateManager* instance = nullptr;

/** Lock for accessing the instance global.  */
CCriticalSection instanceLock;

} // anonymous namespace

ChainstateManager::ChainstateManager ()
  : blockMap(mapBlockIndex), activeChain(chainActive),
    blockTree(*pblocktree), coinsTip(*pcoinsTip)
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
}

ChainstateManager& ChainstateManager::Get ()
{
  LOCK (instanceLock);
  assert (instance != nullptr);
  return *instance;
}
