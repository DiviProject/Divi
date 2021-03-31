#include <MinimumFeeCoinSelectionAlgorithm.h>

#include <algorithm>
#include <SignatureSizeEstimator.h>
#include <primitives/transaction.h>
#include <WalletTx.h>
#include <FeeAndPriorityCalculator.h>
#include <FeeRate.h>
#include <version.h>

extern CFeeRate minRelayTxFee;

MinimumFeeCoinSelectionAlgorithm::MinimumFeeCoinSelectionAlgorithm(
    const CKeyStore& keyStore
    ): keyStore_(keyStore)
{
}
struct InputToSpendAndSigSize
{
    const COutput* outputRef;
    CAmount value;
    unsigned sigSize;

    InputToSpendAndSigSize(
        const COutput& output,
        const CKeyStore& keyStore
        ): outputRef(&output)
        , value(outputRef->Value())
        , sigSize(
            SignatureSizeEstimator::MaxBytesNeededForSigning(
                keyStore,
                outputRef->tx->vout[outputRef->i].scriptPubKey)+40u)
    {
    }
};


std::set<COutput> MinimumFeeCoinSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    CTransaction initialTransaction = CTransaction(transactionToSelectCoinsFor);
    const unsigned initialByteSize = ::GetSerializeSize(initialTransaction, SER_NETWORK, PROTOCOL_VERSION);
    const CAmount nTargetValue = transactionToSelectCoinsFor.GetValueOut();
    constexpr unsigned nominalChangeOutputSize = 34u; // P2PKH change address

    CAmount maximumAmountAvailable = 0;
    std::vector<InputToSpendAndSigSize> inputsToSpendAndSignatureSizeEstimates;
    inputsToSpendAndSignatureSizeEstimates.reserve(vCoins.size());
    for(const COutput& input: vCoins)
    {
        inputsToSpendAndSignatureSizeEstimates.emplace_back(input,keyStore_);
        maximumAmountAvailable+= input.Value();
    }


    bool success = false;
    std::set<COutput> inputsSelected;
    unsigned limitNumberOfFeeIncrements = std::max((initialByteSize+nominalChangeOutputSize+CFeeRate::FEE_INCREMENT_STEPSIZE-1)/CFeeRate::FEE_INCREMENT_STEPSIZE,1u);
    while(!success)
    {
        inputsSelected.clear();
        unsigned maximumTxSize = CFeeRate::FEE_INCREMENT_STEPSIZE*limitNumberOfFeeIncrements -1;
        const unsigned availableTxSize = static_cast<double>(maximumTxSize - initialByteSize - nominalChangeOutputSize);
        const CAmount minimumNoDustChange = FeeAndPriorityCalculator::instance().MinimumValueForNonDust();
        const CAmount minimumRelayFee = minRelayTxFee.GetFee(maximumTxSize);
        const CAmount totalAmountNeeded = nTargetValue + minimumNoDustChange + minimumRelayFee;
        if(totalAmountNeeded > maximumAmountAvailable) return {};

        std::sort(
            inputsToSpendAndSignatureSizeEstimates.begin(),
            inputsToSpendAndSignatureSizeEstimates.end(),
            [totalAmountNeeded,availableTxSize](const InputToSpendAndSigSize& inputA, const InputToSpendAndSigSize& inputB)
            {
                const CAmount gapA = availableTxSize*inputA.outputRef->Value() - inputA.sigSize*totalAmountNeeded;
                const CAmount gapB = availableTxSize*inputB.outputRef->Value() - inputB.sigSize*totalAmountNeeded;
                return gapA > gapB || (gapA == gapB && inputA.outputRef->Value() > inputB.outputRef->Value() );
            });
        CAmount amountCovered =0;
        unsigned cummulativeByteSize = initialByteSize + nominalChangeOutputSize;
        for(const InputToSpendAndSigSize& inputAndSigSize: inputsToSpendAndSignatureSizeEstimates)
        {
            inputsSelected.insert(*inputAndSigSize.outputRef);
            amountCovered += inputAndSigSize.outputRef->Value();
            cummulativeByteSize += inputAndSigSize.sigSize;
            if(cummulativeByteSize > maximumTxSize) break;
            if(amountCovered >= totalAmountNeeded)
            {
                success = true;
                fees = minimumRelayFee;
                return inputsSelected;
            }
        }
        ++limitNumberOfFeeIncrements;
    }
    return {};
}