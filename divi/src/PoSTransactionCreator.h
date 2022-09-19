#ifndef COINSTAKE_CREATOR_H
#define COINSTAKE_CREATOR_H

#include <stdint.h>
#include <amount.h>
#include <set>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <I_PoSTransactionCreator.h>
#include <I_BlockProofProver.h>
#include <NonDeletionDeleter.h>

class CBlock;
class CMutableTransaction;
class CTransaction;
class CChainParams;
class I_BlockIncentivesPopulator;
class CChain;
class I_ProofOfStakeGenerator;
class I_BlockSubsidyProvider;
class BlockMap;
class StakedCoins;
struct StakableCoin;
class Settings;
class I_StakingWallet;

class PoSTransactionCreator: public I_PoSTransactionCreator, public I_BlockProofProver
{
private:
    const Settings& settings_;
    const CChainParams& chainParameters_;
    const CChain& activeChain_;
    const BlockMap& blockIndexByHash_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const I_BlockIncentivesPopulator& incentives_;
    const I_ProofOfStakeGenerator& proofGenerator_;
    mutable std::unique_ptr<StakedCoins> stakedCoins_;
    std::unique_ptr<I_StakingWallet,NonDeletionDeleter<I_StakingWallet>> wallet_;
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps_;
    mutable int64_t hashproofTimestampMinimumValue_;

    void CombineUtxos(
        const CAmount& stakeSplit,
        CMutableTransaction& txNew,
        CAmount& nCredit,
        std::vector<const CTransaction*>& walletTransactions) const;

    bool SetSuportedStakingScript(
        const StakableCoin& stakableCoin,
        CMutableTransaction& txNew) const;

    bool SelectCoins() const;

    bool FindHashproof(
        const CBlockIndex* chainTip,
        unsigned int nBits,
        unsigned int& nTxNewTime,
        const StakableCoin& stakeData,
        CMutableTransaction& txNew) const;

    const StakableCoin* FindProofOfStake(
        const CBlockIndex* chainTip,
        uint32_t blockBits,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime,
        bool& isVaultScript) const;

    void AppendBlockRewardPayoutsToTransaction(
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake) const;
    void SplitOrCombineUTXOS(
        const CAmount stakeSplit,
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake,
        const StakableCoin& stakeData,
        std::vector<const CTransaction*>& vwtxPrev) const;
public:
    PoSTransactionCreator(
        const Settings& settings,
        const CChainParams& chainParameters,
        const CChain& activeChain,
        const BlockMap& mapBlockIndex,
        const I_BlockSubsidyProvider& blockSubsidies,
        const I_BlockIncentivesPopulator& incentives,
        const I_ProofOfStakeGenerator& proofGenerator,
        std::map<unsigned int, unsigned int>& hashedBlockTimestamps);
    ~PoSTransactionCreator();
    bool CreateProofOfStake(
        const CBlockIndex* chainTip,
        CBlock& block) const override;

    void setWallet(I_StakingWallet& wallet);
    bool attachBlockProof(
        const CBlockIndex* chainTip,
        CBlock& block) const override;
};
#endif // COINSTAKE_CREATOR_H
