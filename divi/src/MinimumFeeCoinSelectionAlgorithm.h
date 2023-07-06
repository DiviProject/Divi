#ifndef MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#define MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H
#include <I_CoinSelectionAlgorithm.h>
#include <I_UtxoPriorityAlgorithm.h>
#include <memory>

class CKeyStore;
class I_SignatureSizeEstimator;
class CFeeRate;

enum class ChangeOutputType
{
    P2PKH = 34u,
    VAULT = 59u
};

struct InputToSpendAndSigSize
{
    const COutput* outputRef;
    CAmount value;
    unsigned sigSize;
    InputToSpendAndSigSize(
        const COutput& output,
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator);
};

class BasicCoinSelectionAlgorithm final: public I_CoinSelectionAlgorithm
{
private:
    const CKeyStore& keyStore_;
    const I_SignatureSizeEstimator& estimator_;
    const CFeeRate& minRelayTxFee_;
    const ChangeOutputType changeOutputType_;
    const I_UtxoPriorityAlgorithm& utxoPriorityAlgorithm_;

public:
    BasicCoinSelectionAlgorithm(
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator,
        const CFeeRate& minRelayTxFee,
        const ChangeOutputType changeOutputType,
        const I_UtxoPriorityAlgorithm& utxoPriorityAlgorithm);
    bool isSelectable(const COutput& coin) const override { return true; };
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override;
};

class MinimumFeeCoinSelectionAlgorithm final: public I_CoinSelectionAlgorithm, private I_UtxoPriorityAlgorithm
{
private:
    const CFeeRate& minRelayTxFee_;
    const ChangeOutputType changeOutputType_;
    std::unique_ptr<BasicCoinSelectionAlgorithm> basicCoinSelectionAlgorithm_;
    bool operator()(const CAmount targetAmount, const InputToSpendAndSigSize& first, const InputToSpendAndSigSize& second) const override;

public:
    MinimumFeeCoinSelectionAlgorithm(
        const CKeyStore& keyStore,
        const I_SignatureSizeEstimator& estimator,
        const CFeeRate& minRelayTxFee,
        const ChangeOutputType changeOutputType);
    bool isSelectable(const COutput& coin) const override { return true; };
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override;
};
#endif// MINIMUM_FEE_COIN_SELECTION_ALGORITHM_H