#ifndef I_CHAIN_EXTENSION_SERVICE_H
#define I_CHAIN_EXTENSION_SERVICE_H
class CBlock;
class ChainstateManager;
class CSporkManager;
class CValidationState;
class CBlockIndex;
class CDiskBlockPos;

class I_ChainExtensionService
{
public:
    virtual ~I_ChainExtensionService(){}

    virtual bool assignBlockIndex(
        CBlock& block,
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        CBlockIndex** ppindex,
        CDiskBlockPos* dbp,
        bool fAlreadyCheckedBlock) const = 0;

    virtual bool updateActiveChain(
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        const CBlock* pblock,
        bool fAlreadyChecked) const = 0;
};
#endif// I_CHAIN_EXTENSION_SERVICE_H