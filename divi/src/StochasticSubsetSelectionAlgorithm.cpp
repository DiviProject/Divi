#include <StochasticSubsetSelectionAlgorithm.h>

#include <algorithm>
#include <primitives/transaction.h>
#include <random.h>
#include <algorithm>

struct CompareValueOnly {
    bool operator()(const COutput& t1,
                    const COutput& t2) const
    {
        return t1.Value() < t2.Value();
    }
};


static void FilterToKeepConfirmedAndSpendableOutputs(
    const TxConfirmationChecker& txChecker,
    int nConfMine,
    int nConfTheirs,
    std::vector<COutput>& vCoins)
{
    auto outputSuitabilityCheck = [&txChecker,nConfMine,nConfTheirs](const COutput& output)
    {
        return !output.fSpendable || output.nDepth < txChecker(*output.tx,nConfMine,nConfTheirs);
    };
    vCoins.erase(std::remove_if(vCoins.begin(),vCoins.end(),outputSuitabilityCheck),vCoins.end());
}

static void ApproximateSmallestCoinSubsetForPayment(
    std::vector<COutput> valuedCoins,
    const CAmount& initialEstimateOfBestSubsetTotalValue,
    const CAmount& nTargetValue,
    std::vector<bool>& selectedCoinStatusByIndex,
    CAmount& smallestTotalValueForSelectedSubset,
    int iterations = 1000)
{
    std::vector<bool> inclusionStatusByIndex;

    selectedCoinStatusByIndex.assign(valuedCoins.size(), true);
    smallestTotalValueForSelectedSubset = initialEstimateOfBestSubsetTotalValue;

    for (int nRep = 0; nRep < iterations && smallestTotalValueForSelectedSubset != nTargetValue; nRep++)
    {
        inclusionStatusByIndex.assign(valuedCoins.size(), false);
        CAmount totalValueOfSelectedSubset = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < valuedCoins.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? FastRandomContext().rand32() & 1 : !inclusionStatusByIndex[i])
                {
                    const CAmount outputValue = valuedCoins[i].Value();
                    totalValueOfSelectedSubset += outputValue;
                    inclusionStatusByIndex[i] = true;
                    if (totalValueOfSelectedSubset >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (totalValueOfSelectedSubset < smallestTotalValueForSelectedSubset)
                        {
                            smallestTotalValueForSelectedSubset = totalValueOfSelectedSubset;
                            selectedCoinStatusByIndex = inclusionStatusByIndex;
                        }
                        totalValueOfSelectedSubset -= outputValue;
                        inclusionStatusByIndex[i] = false;
                    }
                }
            }
        }
    }
}

static bool SelectCoinsMinConf(
    const TxConfirmationChecker& txConfirmationChecker,
    const CAmount& nTargetValue,
    int nConfMine,
    int nConfTheirs,
    std::vector<COutput> vCoins,
    std::set<COutput>& setCoinsRet,
    CAmount& nValueRet)
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    COutput coinToSpendAsFallBack;
    bool fallBackCoinWasFound = coinToSpendAsFallBack.IsValid();
    std::vector<COutput> smallValuedCoins;
    CAmount totalOfSmallValuedCoins = 0;

    std::random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);
    FilterToKeepConfirmedAndSpendableOutputs(txConfirmationChecker,nConfMine,nConfTheirs,vCoins);
    for(const COutput &output: vCoins)
    {
        const CAmount outputAmount = output.Value();
        if (outputAmount < nTargetValue + CENT)
        {
            smallValuedCoins.push_back(output);
            totalOfSmallValuedCoins += outputAmount;
        }
        else if (!coinToSpendAsFallBack.IsValid() || outputAmount < coinToSpendAsFallBack.Value())
        {
            coinToSpendAsFallBack = output;
            fallBackCoinWasFound = coinToSpendAsFallBack.IsValid();
        }
    }

    if (totalOfSmallValuedCoins == nTargetValue)
    {
        for (unsigned int i = 0; i < smallValuedCoins.size(); ++i)
        {
            setCoinsRet.insert(smallValuedCoins[i]);
            nValueRet += smallValuedCoins[i].Value();
        }
        return true;
    }

    if (totalOfSmallValuedCoins < nTargetValue)
    {
        if (!fallBackCoinWasFound) return false;
        setCoinsRet.insert(coinToSpendAsFallBack);
        nValueRet += coinToSpendAsFallBack.Value();
        return true;
    }

    // Solve subset sum by stochastic approximation
    std::sort(smallValuedCoins.rbegin(), smallValuedCoins.rend(), CompareValueOnly());
    std::vector<bool> selectedCoinStatusByIndex;
    CAmount totalValueOfSelectedSubset;

    ApproximateSmallestCoinSubsetForPayment(smallValuedCoins, totalOfSmallValuedCoins, nTargetValue, selectedCoinStatusByIndex, totalValueOfSelectedSubset, 1000);
    if (totalValueOfSelectedSubset != nTargetValue && totalOfSmallValuedCoins >= nTargetValue + CENT)
    {
        ApproximateSmallestCoinSubsetForPayment(smallValuedCoins, totalOfSmallValuedCoins, nTargetValue + CENT, selectedCoinStatusByIndex, totalValueOfSelectedSubset, 1000);
    }

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    const bool haveBadCoinSubset = totalValueOfSelectedSubset != nTargetValue && totalValueOfSelectedSubset < nTargetValue + CENT;
    if (fallBackCoinWasFound && (haveBadCoinSubset || coinToSpendAsFallBack.Value() <= totalValueOfSelectedSubset))
    {
        setCoinsRet.insert(coinToSpendAsFallBack);
        nValueRet += coinToSpendAsFallBack.Value();
    }
    else
    {
        for (unsigned int i = 0; i < smallValuedCoins.size(); i++)
        {
            if (selectedCoinStatusByIndex[i])
            {
                setCoinsRet.insert(smallValuedCoins[i]);
                nValueRet += smallValuedCoins[i].Value();
            }
        }
    }
    return true;
}

StochasticSubsetSelectionAlgorithm::StochasticSubsetSelectionAlgorithm(
    TxConfirmationChecker txConfirmationChecker,
    const bool& allowSpendingZeroConfirmationOutputs
    ): txConfirmationChecker_(txConfirmationChecker)
    , allowSpendingZeroConfirmationOutputs_(allowSpendingZeroConfirmationOutputs)
{
}

std::set<COutput> StochasticSubsetSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    CAmount nTargetValue = fees + transactionToSelectCoinsFor.GetValueOut();
    CAmount nValueRet = 0;
    std::set<COutput> setCoinsRet;
    bool enoughCoins = SelectCoinsMinConf(txConfirmationChecker_,nTargetValue, 1, 6, vCoins, setCoinsRet, nValueRet) ||
                SelectCoinsMinConf(txConfirmationChecker_,nTargetValue, 1, 1, vCoins, setCoinsRet, nValueRet) ||
                (allowSpendingZeroConfirmationOutputs_ &&
                SelectCoinsMinConf(txConfirmationChecker_,nTargetValue, 0, 1, vCoins, setCoinsRet, nValueRet));
    if(!enoughCoins)
    {
        setCoinsRet.clear();
    }
    return setCoinsRet;
}