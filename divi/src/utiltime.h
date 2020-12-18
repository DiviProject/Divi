// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTILTIME_H
#define BITCOIN_UTILTIME_H

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <stdint.h>
#include <string>

int64_t GetTime();
int64_t GetTimeMillis();
int64_t GetTimeMicros();
void SetMockTime(int64_t nMockTimeIn);
void MilliSleep(int64_t n);

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime);
std::string DurationToDHMS(int64_t nDurationTime);

// Condition variable that gets notified when the mocktime is changed.
// This can be used to build time-based logic that quickly wakes up
// in tests when we use mocktime.
extern boost::condition_variable cvMockTimeChanged;

// Mutex to use for waiting on cvMockTimeChanged.
extern boost::mutex csMockTime;

#endif // BITCOIN_UTILTIME_H
