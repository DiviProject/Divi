#include <ProofOfStakeModule.h>

#include <LegacyPoSStakeModifierService.h>
#include <PoSStakeModifierService.h>
#include <ProofOfStakeGenerator.h>
#include <chainparams.h>

ProofOfStakeModule::ProofOfStakeModule(
    const CChainParams& chainParameters,
    CChain& activeChain,
    BlockMap& blockIndexByHash
    ): legacyStakeModifierService_(new LegacyPoSStakeModifierService(blockIndexByHash,activeChain))
    , stakeModifierService_(new PoSStakeModifierService(*legacyStakeModifierService_, blockIndexByHash))
    , proofGenerator_(new ProofOfStakeGenerator(*stakeModifierService_,chainParameters.GetMinCoinAgeForStaking()))
{

}
ProofOfStakeModule::~ProofOfStakeModule()
{
    proofGenerator_.reset();
    stakeModifierService_.reset();
    legacyStakeModifierService_.reset();
}

I_ProofOfStakeGenerator& ProofOfStakeModule::proofOfStakeGenerator()
{
    return *proofGenerator_;
}