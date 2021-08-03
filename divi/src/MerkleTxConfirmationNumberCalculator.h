#ifndef MERKLE_TX_CONFIRMATION_CALCULATOR_H
#define MERKLE_TX_CONFIRMATION_CALCULATOR_H
#include <boost/thread/recursive_mutex.hpp>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <utility>
class CBlockIndex;
class CChain;
class BlockMap;
class CTxMemPool;
class CMerkleTx;

template <typename MutexObj>
class AnnotatedMixin;

class MerkleTxConfirmationNumberCalculator final: public I_MerkleTxConfirmationNumberCalculator
{
private:
    const CChain& activeChain_;
    const BlockMap& blockIndices_;
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;
public:
    MerkleTxConfirmationNumberCalculator(
        const CChain& activeChain,
        const BlockMap& blockIndices,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS);
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const;
    int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const;
};

#endif// MERKLE_TX_CONFIRMATION_CALCULATOR_H