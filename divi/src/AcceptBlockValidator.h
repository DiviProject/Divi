#ifndef ACCEPT_BLOCK_VALIDATOR_H
#define ACCEPT_BLOCK_VALIDATOR_H
#include <I_BlockValidator.h>
#include <map>
#include <NodeId.h>
#include <uint256.h>

class CChainParams;
class ChainstateManager;
class CSporkManager;
class CValidationState;
class CNode;
class CDiskBlockPos;
class CCriticalSection;

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

class AcceptBlockValidator final: public I_BlockValidator
{
private:
    const I_ChainExtensionService& chainExtensionService_;
    CCriticalSection& mainCriticalSection_;
    const CChainParams& chainParameters_;
    std::map<uint256, NodeId>& peerIdByBlockHash_;
    ChainstateManager& chainstate_;
    const CSporkManager& sporkManager_;
    CValidationState& state_;
    CNode* pfrom_;
    CDiskBlockPos* dbp_;
public:
    AcceptBlockValidator(
        const I_ChainExtensionService& chainExtensionService,
        CCriticalSection& mainCriticalSection,
        const CChainParams& chainParameters,
        std::map<uint256, NodeId>& peerIdByBlockHash,
        ChainstateManager& chainstate,
        const CSporkManager& sporkManager,
        CValidationState& state,
        CNode* pfrom,
        CDiskBlockPos* dbp);

    std::pair<CBlockIndex*, bool> validateAndAssignBlockIndex(CBlock& block, bool blockChecked) const override;
    bool connectActiveChain(CBlockIndex* blockIndex, const CBlock& block, bool blockChecked) const override;
    bool checkBlockRequirements(const CBlock& block, bool& checked) const override;
};
#endif// ACCEPT_BLOCK_VALIDATOR_H