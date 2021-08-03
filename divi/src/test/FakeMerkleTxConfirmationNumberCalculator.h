#ifndef FAKE_MERKLE_TX_CONFIRMATION_CALCULATOR_H
#define FAKE_MERKLE_TX_CONFIRMATION_CALCULATOR_H
#include <boost/thread/recursive_mutex.hpp>

#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <utility>
class CBlockIndex;
class CChain;
class BlockMap;
class CMerkleTx;

class FakeMerkleTxConfirmationNumberCalculator final: public I_MerkleTxConfirmationNumberCalculator
{
private:
    const CChain& activeChain_;
    const BlockMap& blockIndices_;
public:
    FakeMerkleTxConfirmationNumberCalculator(
        const CChain& activeChain,
        const BlockMap& blockIndices);
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const;
    int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const;
};
#endif// FAKE_MERKLE_TX_CONFIRMATION_CALCULATOR_H