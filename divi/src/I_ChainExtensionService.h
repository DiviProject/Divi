#ifndef I_CHAIN_EXTENSION_SERVICE_H
#define I_CHAIN_EXTENSION_SERVICE_H
class CBlock;
class ChainstateManager;
class CSporkManager;
class CValidationState;
class CBlockIndex;
class CDiskBlockPos;
#include <NodeId.h>

class I_ChainExtensionService
{
public:
    virtual ~I_ChainExtensionService(){}

    virtual void recordBlockSource(
        const uint256& blockHash,
        NodeId nodeId) const =0;
    virtual bool assignBlockIndex(
        CBlock& block,
        CValidationState& state,
        CBlockIndex** ppindex,
        CDiskBlockPos* dbp) const = 0;

    virtual bool updateActiveChain(
        CValidationState& state,
        const CBlock* pblock,
        bool fAlreadyChecked) const = 0;

    virtual bool invalidateBlock(CValidationState& state, CBlockIndex* blockIndex, const bool updateCoinDatabaseOnly) const = 0;
    virtual bool reconsiderBlock(CValidationState& state, CBlockIndex* pindex) const = 0;
};
#endif// I_CHAIN_EXTENSION_SERVICE_H