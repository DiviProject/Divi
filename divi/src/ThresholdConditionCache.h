#ifndef THRESHOLD_CONDITION_CACHE_H
#define THRESHOLD_CONDITION_CACHE_H
#include <map>
#include <BIP9Deployment.h>
class CBLockIndex;
struct ThresholdConditionCache: public std::map<const CBlockIndex*, ThresholdState>
{
};
#endif // THRESHOLD_CONDITION_CACHE_H