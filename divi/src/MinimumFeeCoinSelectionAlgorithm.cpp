#include <MinimumFeeCoinSelectionAlgorithm.h>

#include <algorithm>
#include <SignatureSizeEstimator.h>
#include <primitives/transaction.h>
#include <WalletTx.h>
#include <FeeAndPriorityCalculator.h>
#include <FeeRate.h>
#include <version.h>
#include <I_SignatureSizeEstimator.h>
#include <defaultValues.h>


MinimumFeeCoinSelectionAlgorithm::MinimumFeeCoinSelectionAlgorithm(
    const CKeyStore& keyStore,
    const I_SignatureSizeEstimator& estimator,
    const CFeeRate& minRelayTxFee
    ): keyStore_(keyStore)
    , estimator_(estimator)
    , minRelayTxFee_(minRelayTxFee)
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
            estimator.MaxBytesNeededForScriptSig(
                keyStore,
                outputRef->scriptPubKey()))
    {
    }
};


std::set<COutput> MinimumFeeCoinSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    constexpr unsigned nominalChangeOutputSize = 34u; // P2PKH change address
    CTransaction initialTransaction = CTransaction(transactionToSelectCoinsFor);
    const unsigned initialByteSize = ::GetSerializeSize(initialTransaction, SER_NETWORK, PROTOCOL_VERSION);

    CAmount maximumAmountAvailable = 0;
    std::vector<InputToSpendAndSigSize> inputsToSpendAndSignatureSizeEstimates;
    inputsToSpendAndSignatureSizeEstimates.reserve(vCoins.size());
    for(const COutput& input: vCoins)
    {
        inputsToSpendAndSignatureSizeEstimates.emplace_back(input,keyStore_,estimator_);
        maximumAmountAvailable+= input.Value();
    }
    const CAmount nTargetValue = transactionToSelectCoinsFor.GetValueOut();

    std::set<COutput> inputsSelected;
    inputsSelected.clear();
    const CAmount minimumNoDustChange = FeeAndPriorityCalculator::instance().MinimumValueForNonDust();
    const CAmount totalAmountNeeded = nTargetValue + minimumNoDustChange + fees;
    if(totalAmountNeeded > maximumAmountAvailable) return {};

    std::sort(
        inputsToSpendAndSignatureSizeEstimates.begin(),
        inputsToSpendAndSignatureSizeEstimates.end(),
        [totalAmountNeeded,this](const InputToSpendAndSigSize& inputA, const InputToSpendAndSigSize& inputB)
        {
            const CAmount gapA = inputA.outputRef->Value() - minRelayTxFee_.GetFee(inputA.sigSize);
            const CAmount gapB = inputB.outputRef->Value() - minRelayTxFee_.GetFee(inputB.sigSize);
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
        amountCovered += inputAndSigSize.outputRef->Value() - minRelayTxFee_.GetFee(inputAndSigSize.sigSize);
        cummulativeByteSize += inputAndSigSize.sigSize;
        if(cummulativeByteSize >= MAX_STANDARD_TX_SIZE) return {};
        if( amountCovered >= totalAmountNeeded )
        {
            fees += minRelayTxFee_.GetFee(cummulativeByteSize);
            if(minRelayTxFee_.GetMaxTxFee() < fees)
            {
                return {};
            }
            else
            {
                return inputsSelected;
            }
        }
    }
    return {};
}