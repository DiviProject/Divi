#ifndef FORK_WARNING_HELPERS_H
#define FORK_WARNING_HELPERS_H
class CBlockIndex;
class CCriticalSection;
class Settings;
void CheckForkWarningConditions(const Settings& settings, CCriticalSection& mainCriticalSection, bool isInitialBlockDownload);
void CheckForkWarningConditionsOnNewFork(const Settings& settings, CCriticalSection& mainCriticalSection,const CBlockIndex* pindexNewForkTip, bool isInitialBlockDownload);
#endif// FORK_WARNING_HELPERS_H