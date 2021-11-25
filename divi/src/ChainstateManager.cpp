#include "ChainstateManager.h"

extern BlockMap mapBlockIndex;
extern CChain chainActive;
extern CBlockTreeDB* pblocktree;
extern CCoinsViewCache* pcoinsTip;

ChainstateManager::ChainstateManager ()
  : blockMap(mapBlockIndex), activeChain(chainActive),
    blockTree(*pblocktree), coinsTip(*pcoinsTip)
{}
