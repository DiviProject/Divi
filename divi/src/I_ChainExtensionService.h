#ifndef I_CHAIN_EXTENSION_SERVICE_H
#define I_CHAIN_EXTENSION_SERVICE_H
class CBlock;
class ChainstateManager;
class CSporkManager;
class CValidationState;
class CBlockIndex;
class CDiskBlockPos;
#include <utility>

class I_ChainExtensionService
{
public:
    virtual ~I_ChainExtensionService(){}

    virtual std::pair<CBlockIndex*, bool> assignBlockIndex(
        CBlock& block,
        CValidationState& state,
        CDiskBlockPos* dbp) const = 0;

    virtual bool updateActiveChain(
        CValidationState& state,
        const CBlock* pblock) const = 0;

    virtual bool invalidateBlock(CValidationState& state, CBlockIndex* blockIndex, const bool updateCoinDatabaseOnly) const = 0;
    virtual bool reconsiderBlock(CValidationState& state, CBlockIndex* pindex) const = 0;
    virtual bool connectGenesisBlock() const = 0;
};
#endif// I_CHAIN_EXTENSION_SERVICE_H