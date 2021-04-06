#include <MinimumFeeCoinSelectionAlgorithm.h>

#include <algorithm>
#include <SignatureSizeEstimator.h>
#include <primitives/transaction.h>
#include <WalletTx.h>
#include <FeeAndPriorityCalculator.h>
#include <FeeRate.h>
#include <version.h>
#include <I_SignatureSizeEstimator.h>

extern CFeeRate minRelayTxFee;
extern CAmount maxTxFee;

MinimumFeeCoinSelectionAlgorithm::MinimumFeeCoinSelectionAlgorithm(
    const CKeyStore& keyStore,
    const I_SignatureSizeEstimator& estimator
    ): keyStore_(keyStore)
    , estimator_(estimator)
{
}
struct InputToSpendAndSigSize
{
    const COutput* outputRef;
    CAmount value;
    unsigned sigSize;

    InputToSpendAndSigSize(
        const COutput& output,
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator
        ): outputRef(&output)
        , value(outputRef->Value())
        , sigSize(
            estimator.MaxBytesNeededForSigning(
                keyStore,
                outputRef->scriptPubKey())+40u)
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
        inputsToSpendAndSignatureSizeEstimates.emplace_back(input,keyStore_,estimator_);
        maximumAmountAvailable+= input.Value();
    }


    constexpr unsigned MAX_TRANSACTION_SIZE = 100000u;
    std::set<COutput> inputsSelected;
    inputsSelected.clear();
    const CAmount minimumNoDustChange = FeeAndPriorityCalculator::instance().MinimumValueForNonDust();
    const CAmount totalAmountNeeded = nTargetValue + minimumNoDustChange+fees;
    if(totalAmountNeeded > maximumAmountAvailable) return {};

    std::sort(
        inputsToSpendAndSignatureSizeEstimates.begin(),
        inputsToSpendAndSignatureSizeEstimates.end(),
        [totalAmountNeeded](const InputToSpendAndSigSize& inputA, const InputToSpendAndSigSize& inputB)
        {
            const CAmount gapA = inputA.outputRef->Value() - minRelayTxFee.GetFee(inputA.sigSize);
            const CAmount gapB = inputB.outputRef->Value() - minRelayTxFee.GetFee(inputB.sigSize);
            if(gapA >= totalAmountNeeded && gapB >= totalAmountNeeded)
            {
                return inputA.sigSize < inputB.sigSize;
            }
            return gapA > gapB || (gapA == gapB && inputA.sigSize < inputB.sigSize);
        });
    CAmount amountCovered =0;
    unsigned cummulativeByteSize = initialByteSize + nominalChangeOutputSize;
    for(const InputToSpendAndSigSize& inputAndSigSize: inputsToSpendAndSignatureSizeEstimates)
    {
        inputsSelected.insert(*inputAndSigSize.outputRef);
        amountCovered += inputAndSigSize.outputRef->Value();
        cummulativeByteSize += inputAndSigSize.sigSize;
        if(cummulativeByteSize >= MAX_TRANSACTION_SIZE) return {};
        if(amountCovered >= totalAmountNeeded)
        {
            fees += std::min(minRelayTxFee.GetFee(cummulativeByteSize),maxTxFee);
            return inputsSelected;
        }
    }
    return {};
}