#ifndef PROOF_OF_STAKE_MODULE_H
#define PROOF_OF_STAKE_MODULE_H
#include <memory>
#include <I_ProofOfStakeGenerator.h>
class I_PoSStakeModifierService;
class I_PoSStakeModifierService;
class CChainParams;
class CChain;
class BlockMap;

class ProofOfStakeModule
{
    std::unique_ptr<I_PoSStakeModifierService> legacyStakeModifierService_;
    std::unique_ptr<I_PoSStakeModifierService> stakeModifierService_;
    std::unique_ptr<I_ProofOfStakeGenerator> proofGenerator_;
public:
    ProofOfStakeModule(
        const CChainParams& chainParameters,
        const CChain& activeChain,
        const BlockMap& blockIndexByHash);
    ~ProofOfStakeModule();
    const I_ProofOfStakeGenerator& proofOfStakeGenerator() const;
};
#endif// PROOF_OF_STAKE_MODULE_H