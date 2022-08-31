#ifndef CHAIN_SYNC_HELPERS_H
#define CHAIN_SYNC_HELPERS_H
#include <stdint.h>
class CBlockIndex;
class CCriticalSection;
class Settings;
void InitializeBestHeaderBlockIndex();
void updateBestHeaderBlockIndex(const CBlockIndex* otherBlockIndex, bool compareByWorkOnly);
int GetBestHeaderBlockHeight();
int64_t GetBestHeaderBlocktime();
bool IsInitialBlockDownload(CCriticalSection& mainCriticalSection, const Settings& settings);
#endif// CHAIN_SYNC_HELPERS_H