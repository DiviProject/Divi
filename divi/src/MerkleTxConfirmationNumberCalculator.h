#ifndef MERKLE_TX_CONFIRMATION_CALCULATOR_H
#define MERKLE_TX_CONFIRMATION_CALCULATOR_H
#include <boost/thread/recursive_mutex.hpp>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <utility>
#include <unordered_map>
#include <uint256.h>
#include <sync.h>
class CBlockIndex;
class CChain;
class BlockMap;
class CTxMemPool;
class CMerkleTx;
class CCriticalSection;

struct HashHasher {
    size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};
class MerkleTxConfirmationNumberCalculator final: public I_MerkleTxConfirmationNumberCalculator
{
private:
    const CChain& activeChain_;
    const BlockMap& blockIndices_;
    const int coinbaseConfirmationsForMaturity_;
    const CTxMemPool& mempool_;
    CCriticalSection& mainCS_;
    mutable CCriticalSection cacheLock_;
    mutable std::unordered_map<uint256,const CBlockIndex*,HashHasher> cachedConfirmationLookups_;
    static const unsigned DEPTH;
public:
    MerkleTxConfirmationNumberCalculator(
        const CChain& activeChain,
        const BlockMap& blockIndices,
        const int coinbaseConfirmationsForMaturity,
        const CTxMemPool& mempool,
        CCriticalSection& mainCS);
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const override;
    int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const override;
    int GetBlocksToMaturity(const CMerkleTx& merkleTx) const override;
};

#endif// MERKLE_TX_CONFIRMATION_CALCULATOR_H