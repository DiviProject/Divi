

#ifndef MOCK_UTXO_BALANCE_CALCULATOR_H
#define MOCK_UTXO_BALANCE_CALCULATOR_H
#include <gmock/gmock.h>
#include <I_TransactionDetailCalculator.h>
#include <amount.h>

class MockUtxoBalanceCalculator: public I_TransactionDetailCalculator<CAmount>
{
public:
    MOCK_CONST_METHOD3(calculate, void(const CWalletTx&, const UtxoOwnershipFilter&, CAmount&));
};
#endif// MOCK_UTXO_BALANCE_CALCULATOR_H
