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

InputToSpendAndSigSize::InputToSpendAndSigSize(
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

BasicCoinSelectionAlgorithm::BasicCoinSelectionAlgorithm(
    const CKeyStore& keyStore,
    const I_SignatureSizeEstimator& estimator,
    const CFeeRate& minRelayTxFee,
    const ChangeOutputType changeOutputType,
    const I_UtxoPriorityAlgorithm& utxoPriorityAlgorithm
    ): keyStore_(keyStore)
    , estimator_(estimator)
    , minRelayTxFee_(minRelayTxFee)
    , changeOutputType_(changeOutputType)
    , utxoPriorityAlgorithm_(utxoPriorityAlgorithm)
{
}

std::set<COutput> BasicCoinSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    const unsigned nominalChangeOutputSize = static_cast<unsigned>(changeOutputType_); // Vault? Vault-Change : P2PKH change address
    CTransaction initialTransaction = CTransaction(transactionToSelectCoinsFor);
    const unsigned initialByteSize = ::GetSerializeSize(initialTransaction, SER_NETWORK, PROTOCOL_VERSION);

    CAmount maximumAmountAvailable = 0;
    std::vector<InputToSpendAndSigSize> inputsToSpendAndSignatureSizeEstimates;
    inputsToSpendAndSignatureSizeEstimates.reserve(vCoins.size());
    std::set<COutPoint> outputsUsed;
    for(const CTxIn& txInput: initialTransaction.vin)
    {
        outputsUsed.insert(txInput.prevout);
    }
    CAmount amountAlreadyCovered = 0;
    fees += minRelayTxFee_.GetFee(initialByteSize);
    for(const COutput& input: vCoins)
    {
        if(outputsUsed.count(COutPoint(input.tx->GetHash(),input.i))>0)
        {
            InputToSpendAndSigSize inputWithSigSize(input,keyStore_,estimator_);
            amountAlreadyCovered += inputWithSigSize.outputRef->Value();
            fees += minRelayTxFee_.GetFee(inputWithSigSize.sigSize);
            continue;
        }
        inputsToSpendAndSignatureSizeEstimates.emplace_back(input,keyStore_,estimator_);
        maximumAmountAvailable+= input.Value();
    }
    const CAmount nTargetValue = transactionToSelectCoinsFor.GetValueOut() - amountAlreadyCovered;

    std::set<COutput> inputsSelected;
    inputsSelected.clear();
    const CAmount minimumNoDustChange = FeeAndPriorityCalculator::instance().MinimumValueForNonDust();
    const CAmount totalAmountNeeded = nTargetValue + minimumNoDustChange + fees;
    if(totalAmountNeeded > maximumAmountAvailable) return {};

    std::sort(
        inputsToSpendAndSignatureSizeEstimates.begin(),
        inputsToSpendAndSignatureSizeEstimates.end(),
        [totalAmountNeeded, this](const InputToSpendAndSigSize& inputA, const InputToSpendAndSigSize& inputB)
        {
            return utxoPriorityAlgorithm_(totalAmountNeeded,inputA,inputB);
        });
    CAmount amountCovered =0;
    unsigned cummulativeByteSize = nominalChangeOutputSize;
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

MinimumFeeCoinSelectionAlgorithm::MinimumFeeCoinSelectionAlgorithm(
    const CKeyStore& keyStore,
    const I_SignatureSizeEstimator& estimator,
    const CFeeRate& minRelayTxFee,
    const ChangeOutputType changeOutputType
    ): minRelayTxFee_(minRelayTxFee)
    , changeOutputType_(changeOutputType)
    , basicCoinSelectionAlgorithm_(
        new BasicCoinSelectionAlgorithm(
            keyStore,
            estimator,
            minRelayTxFee,
            changeOutputType,
            *static_cast<I_UtxoPriorityAlgorithm*>(this)))
{
}

bool MinimumFeeCoinSelectionAlgorithm::operator()(const CAmount targetAmount, const InputToSpendAndSigSize& first, const InputToSpendAndSigSize& second) const
{
    if(changeOutputType_ == ChangeOutputType::VAULT && first.outputRef->nDepth != second.outputRef->nDepth)
    {
        return first.outputRef->nDepth < second.outputRef->nDepth;
    }
    const CAmount gapA = first.outputRef->Value() - minRelayTxFee_.GetFee(first.sigSize);
    const CAmount gapB = second.outputRef->Value() - minRelayTxFee_.GetFee(second.sigSize);
    const bool inputAShouldBePrioritized =
        (gapA >= targetAmount && gapB >= targetAmount)
        ? first.sigSize < second.sigSize
        : gapA > gapB || (gapA == gapB && first.sigSize < second.sigSize);
    return inputAShouldBePrioritized;
}

std::set<COutput> MinimumFeeCoinSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    return basicCoinSelectionAlgorithm_->SelectCoins(transactionToSelectCoinsFor,vCoins,fees);
}