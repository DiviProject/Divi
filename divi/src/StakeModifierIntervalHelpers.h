#ifndef STAKE_MODIFIER_INTERVAL_HELPERS_H
#define STAKE_MODIFIER_INTERVAL_HELPERS_H
#include <stdint.h>
#include <cassert>
static const int MODIFIER_INTERVAL_RATIO = 3;
static const unsigned int MODIFIER_INTERVAL = 60;
static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert(nSection >= 0 && nSection < 64);
    int64_t a = MODIFIER_INTERVAL * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
    return a;
}
static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection = 0; nSection < 64; nSection++) {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}
#endif// STAKE_MODIFIER_INTERVAL_HELPERS_H
