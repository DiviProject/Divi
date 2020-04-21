/**
 * @file       denominations_functions.cpp
 *
 * @brief      Denomination functions for the Zerocoin library.
 *
 * @copyright  Copyright 2017 PIVX Developers
 * @license    This project is released under the MIT license.
 **/
// Copyright (c) 2015-2017 The PIVX Developers

#include "denomination_functions.h"

using namespace libzerocoin;

// -------------------------------------------------------------------------------------------------------
// Number of coins used for either change or a spend given a map of coins used
// -------------------------------------------------------------------------------------------------------
int getNumberOfCoinsUsed(
    const std::map<CoinDenomination, CAmount>& mapChange)
{
    int nChangeCount = 0;
    for (const auto& denom : zerocoinDenomList) {
        nChangeCount += mapChange.at(denom);
    }
    return nChangeCount;
}

// -------------------------------------------------------------------------------------------------------
// Find the max CoinDenomination amongst held coins
// -------------------------------------------------------------------------------------------------------
CoinDenomination getMaxDenomHeld(
    const std::map<CoinDenomination, CAmount>& mapCoinsHeld)
{
    CoinDenomination maxDenom = ZQ_ERROR;
    for (auto& coin : reverse_iterate(zerocoinDenomList)) {
        if (mapCoinsHeld.at(coin)) {
            maxDenom = coin;
            break;
        }
    }
    return maxDenom;
}
// -------------------------------------------------------------------------------------------------------
// Get Exact Amount with CoinsHeld
// -------------------------------------------------------------------------------------------------------
std::map<CoinDenomination, CAmount> getSpendCoins(const CAmount nValueTarget,
    const std::map<CoinDenomination, CAmount> mapOfDenomsHeld)

{
    std::map<CoinDenomination, CAmount> mapUsed;
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : zerocoinDenomList)
        mapUsed.insert(std::pair<CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value and we have the denom
    for (auto& coin : reverse_iterate(zerocoinDenomList)) {
        CAmount nValue = ZerocoinDenominationToAmount(coin);
        do {
            if ((nRemainingValue >= nValue) && (mapUsed.at(coin) < mapOfDenomsHeld.at(coin))) {
                mapUsed.at(coin)++;
                nRemainingValue -= nValue;
            }
        } while ((nRemainingValue >= nValue) && (mapUsed.at(coin) < mapOfDenomsHeld.at(coin)));
    }
    return mapUsed;
}

// -------------------------------------------------------------------------------------------------------
// Get change (no limits)
// -------------------------------------------------------------------------------------------------------
std::map<CoinDenomination, CAmount> getChange(const CAmount nValueTarget)
{
    std::map<CoinDenomination, CAmount> mapChange;
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : zerocoinDenomList)
        mapChange.insert(std::pair<CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value
    for (auto& coin : reverse_iterate(zerocoinDenomList)) {
        CAmount nValue = ZerocoinDenominationToAmount(coin);
        do {
            if (nRemainingValue >= nValue) {
                mapChange.at(coin)++;
                nRemainingValue -= nValue;
            }
        } while (nRemainingValue >= nValue);
    }
    return mapChange;
}

// -------------------------------------------------------------------------------------------------------
// Attempt to use coins held to exactly reach nValueTarget, return mapOfDenomsUsed with the coin set used
// Return false if exact match is not possible
// -------------------------------------------------------------------------------------------------------
bool getIdealSpends(
    const CAmount nValueTarget,
    const std::list<CZerocoinMint>& listMints,
    const std::map<CoinDenomination, CAmount> mapOfDenomsHeld,
    std::map<CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : zerocoinDenomList)
        mapOfDenomsUsed.insert(std::pair<CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value
    for (auto& coin : reverse_iterate(zerocoinDenomList)) {
        for (const CZerocoinMint mint : listMints) {
            if (mint.IsUsed()) continue;
            if (nRemainingValue >= ZerocoinDenominationToAmount(coin) && coin == mint.GetDenomination()) {
                mapOfDenomsUsed.at(coin)++;
                nRemainingValue -= mint.GetDenominationAsAmount();
            }
            if (nRemainingValue < ZerocoinDenominationToAmount(coin)) break;
        }
    }
    return (nRemainingValue == 0);
}

// -------------------------------------------------------------------------------------------------------
// Return a list of Mint coins based on mapOfDenomsUsed and the overall value in nCoinsSpentValue
// -------------------------------------------------------------------------------------------------------
std::vector<CZerocoinMint> getSpends(
    const std::list<CZerocoinMint>& listMints,
    std::map<CoinDenomination, CAmount>& mapOfDenomsUsed,
    CAmount& nCoinsSpentValue)
{
    std::vector<CZerocoinMint> vSelectedMints;
    nCoinsSpentValue = 0;
    for (auto& coin : reverse_iterate(zerocoinDenomList)) {
        do {
            for (const CZerocoinMint mint : listMints) {
                if (mint.IsUsed()) continue;
                if (coin == mint.GetDenomination() && mapOfDenomsUsed.at(coin)) {
                    vSelectedMints.push_back(mint);
                    nCoinsSpentValue += ZerocoinDenominationToAmount(coin);
                    mapOfDenomsUsed.at(coin)--;
                }
            }
        } while (mapOfDenomsUsed.at(coin));
    }
    return vSelectedMints;
}
// -------------------------------------------------------------------------------------------------------
// Find the CoinDenomination with the most number for a given amount
// -------------------------------------------------------------------------------------------------------
CoinDenomination getDenomWithMostCoins(
    const std::map<CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    CoinDenomination maxCoins = ZQ_ERROR;
    CAmount nMaxNumber = 0;
    for (const auto& denom : zerocoinDenomList) {
        CAmount amount = mapOfDenomsUsed.at(denom);
        if (amount > nMaxNumber) {
            nMaxNumber = amount;
            maxCoins = denom;
        }
    }
    return maxCoins;
}
// -------------------------------------------------------------------------------------------------------
// Get the next denomination above the current one. Return ZQ_ERROR if already at the highest
// -------------------------------------------------------------------------------------------------------
CoinDenomination getNextHighestDenom(const CoinDenomination& this_denom)
{
    CoinDenomination nextValue = ZQ_ERROR;
    for (const auto& denom : zerocoinDenomList) {
        if (ZerocoinDenominationToAmount(denom) > ZerocoinDenominationToAmount(this_denom)) {
            nextValue = denom;
            break;
        }
    }
    return nextValue;
}
// -------------------------------------------------------------------------------------------------------
// Get the next denomination below the current one that is also amongst those held.
// Return ZQ_ERROR if none found
// -------------------------------------------------------------------------------------------------------
CoinDenomination getNextLowerDenomHeld(const CoinDenomination& this_denom,
    const std::map<CoinDenomination, CAmount>& mapCoinsHeld)
{
    CoinDenomination nextValue = ZQ_ERROR;
    for (auto& denom : reverse_iterate(zerocoinDenomList)) {
        if ((denom < this_denom) && (mapCoinsHeld.at(denom) != 0)) {
            nextValue = denom;
            break;
        }
    }
    return nextValue;
}

int minimizeChange(
    int nMaxNumberOfSpends,
    int nChangeCount,
    const CoinDenomination nextToMaxDenom,
    const CAmount nValueTarget,
    const std::map<CoinDenomination, CAmount>& mapOfDenomsHeld,
    std::map<CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    // Now find out if possible without using 1 coin such that we have more spends but less change
    // First get set of coins close to value but still less than value (since not exact)
    CAmount nRemainingValue = nValueTarget;
    CAmount AmountUsed = 0;
    int nCoinCount = 0;

    // Re-clear this
    std::map<CoinDenomination, CAmount> savedMapOfDenomsUsed = mapOfDenomsUsed;
    for (const auto& denom : zerocoinDenomList)
        mapOfDenomsUsed.at(denom) = 0;

    // Find the amount this is less than total but uses up higher denoms first,
    // starting at the denom that is not greater than the overall total
    for (const auto& denom : reverse_iterate(zerocoinDenomList)) {
        if (denom <= nextToMaxDenom) {
            CAmount nValue = ZerocoinDenominationToAmount(denom);
            do {
                if ((nRemainingValue > nValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom))) {
                    mapOfDenomsUsed.at(denom)++;
                    nRemainingValue -= nValue;
                    AmountUsed += nValue;
                    nCoinCount++;
                }
            } while ((nRemainingValue > nValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom)));
        }
    }

    // Now work way back up from the bottom filling in with the denom that we have that is just
    // bigger than the remaining amount
    // Shouldn't need more than one coin here?
    for (const auto& denom : zerocoinDenomList) {
        CAmount nValue = ZerocoinDenominationToAmount(denom);
        if ((nValue > nRemainingValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom))) {
            mapOfDenomsUsed.at(denom)++;
            nRemainingValue -= nValue;
            AmountUsed += nValue;
            nCoinCount++;
        }
        if (nRemainingValue < 0) break;
    }

    // This can still result in a case where you've used an extra spend than needed.
    // e.g Spend of 26, while having 1*5 + 4*10
    // First stage may be 2*10+5 (i.e < 26)
    // Second stage can be 3*10+5 (no more fives, so add a 10)
    // So 5 is no longer needed and will become change also

    CAmount nAltChangeAmount = AmountUsed - nValueTarget;
    std::map<CoinDenomination, CAmount> mapAltChange = getChange(nAltChangeAmount);

    // Check if there is overlap between change and spend denominations
    // And if so, remove those that overlap
    for (const auto& denom : zerocoinDenomList) {
        do {
            if (mapAltChange.at(denom) && mapOfDenomsUsed.at(denom)) {
                mapOfDenomsUsed.at(denom)--;
                mapAltChange.at(denom)--;
                nCoinCount--;
                CAmount nValue = ZerocoinDenominationToAmount(denom);
                AmountUsed -= nValue;
            }
        } while (mapAltChange.at(denom) && mapOfDenomsUsed.at(denom));
    }

    // Still possible to have wrong mix. So meet exact amount found above - with least number of coins
    mapOfDenomsUsed = getSpendCoins(AmountUsed, mapOfDenomsHeld);
    nCoinCount = getNumberOfCoinsUsed(mapOfDenomsUsed);

    // Re-calculate change
    nAltChangeAmount = AmountUsed - nValueTarget;
    mapAltChange = getChange(nAltChangeAmount);
    int AltChangeCount = getNumberOfCoinsUsed(mapAltChange);

    // Alternative method yields less mints and is less than MaxNumberOfSpends if true
    if ((AltChangeCount < nChangeCount) && (nCoinCount <= nMaxNumberOfSpends)) {
        return AltChangeCount;
    } else {
        // if we don't meet above go back to what we started with
        mapOfDenomsUsed = savedMapOfDenomsUsed;
        return nChangeCount;
    }
}
