#ifndef I_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H
#define I_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H
#include <utility>
class CBlockIndex;
class CMerkleTx;

class I_MerkleTxConfirmationNumberCalculator
{
public:
    virtual ~I_MerkleTxConfirmationNumberCalculator(){}
    virtual std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const = 0;
    virtual int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const = 0;
};
#endif// I_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H