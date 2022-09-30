#ifndef BLOCK_INDEX_LOADING_H
#define BLOCK_INDEX_LOADING_H
#include <string>
class ChainstateManager;
class Settings;
/** Load the block tree and coins database from disk */
bool LoadBlockIndex(Settings& settings, std::string& strError);
/** Unload database information.  If a ChainstateManager is present,
 *  the block map inside (and all other in-memory information) is unloaded.
 *  Otherwise just local data (e.g. validated but not yet attached
 *  CBlockIndex instances) is removed.  */
void UnloadBlockIndex(ChainstateManager* chainstate);
#endif// BLOCK_INDEX_LOADING_H