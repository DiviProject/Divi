//I_MerkleTxConfirmationNumberCalculator
#ifndef MOCK_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H
#define MOCK_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H
#include <gmock/gmock.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

class MockMerkleTxConfirmationNumberCalculator: public I_MerkleTxConfirmationNumberCalculator
{
public:
    typedef std::pair<const CBlockIndex*,int> BlockIndexAndConfirmationDepth;
    MOCK_CONST_METHOD1(FindConfirmedBlockIndexAndDepth, BlockIndexAndConfirmationDepth(const CMerkleTx& merkleTx));
    MOCK_CONST_METHOD1(GetNumberOfBlockConfirmations, int(const CMerkleTx& merkleTx));
    MOCK_CONST_METHOD1(GetBlocksToMaturity, int(const CMerkleTx& merkleTx));
};
#endif// MOCK_MERKLE_TX_CONFIRMATION_NUMBER_CALCULATOR_H
