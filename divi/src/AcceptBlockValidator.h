#ifndef ACCEPT_BLOCK_VALIDATOR_H
#define ACCEPT_BLOCK_VALIDATOR_H
#include <I_BlockValidator.h>
#include <map>
#include <NodeId.h>
#include <uint256.h>
#include <I_ChainExtensionService.h>

class CChainParams;
class ChainstateManager;
class CValidationState;
class CNode;
class CDiskBlockPos;
class CCriticalSection;


class AcceptBlockValidator final: public I_BlockValidator
{
private:
    const I_ChainExtensionService& chainExtensionService_;
    CCriticalSection& mainCriticalSection_;
    const CChainParams& chainParameters_;
    ChainstateManager& chainstate_;
public:
    AcceptBlockValidator(
        const I_ChainExtensionService& chainExtensionService,
        CCriticalSection& mainCriticalSection,
        const CChainParams& chainParameters,
        ChainstateManager& chainstate);

    bool connectActiveChain(const CBlock& block, CValidationState& state) const override;
    bool checkBlockRequirements(const NodeAndBlockDiskPosition& nodeAndBlockDisk, const CBlock& block, CValidationState& state) const override;
    std::pair<CBlockIndex*, bool> validateAndAssignBlockIndex(const NodeAndBlockDiskPosition& nodeAndBlockDisk, CBlock& block, CValidationState& state) const override;
};
#endif// ACCEPT_BLOCK_VALIDATOR_H