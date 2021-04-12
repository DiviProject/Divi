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

class CWallet;
class CBlock;
class CMutableTransaction;
class CKeyStore;
class CTransaction;
class CChainParams;
class I_SuperblockSubsidyContainer;
class I_BlockIncentivesPopulator;
class CChain;
class I_PoSStakeModifierService;
class I_ProofOfStakeGenerator;
class I_BlockSubsidyProvider;
class BlockMap;
class StakedCoins;
struct StakableCoin;
class Settings;

class PoSTransactionCreator: public I_PoSTransactionCreator
{
private:
    const Settings& settings_;
    const CChainParams& chainParameters_;
    const CChain& activeChain_;
    const BlockMap& mapBlockIndex_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const I_BlockIncentivesPopulator& incentives_;
    const I_ProofOfStakeGenerator& proofGenerator_;
    std::unique_ptr<StakedCoins> stakedCoins_;
    CWallet& wallet_;
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps_;
    int64_t hashproofTimestampMinimumValue_;

    void CombineUtxos(
        const CAmount& stakeSplit,
        CMutableTransaction& txNew,
        CAmount& nCredit,
        std::vector<const CTransaction*>& walletTransactions);

    bool SetSuportedStakingScript(
        const StakableCoin& stakableCoin,
        CMutableTransaction& txNew);

    bool SelectCoins();

    bool FindHashproof(
        const CBlockIndex* chainTip,
        unsigned int nBits,
        unsigned int& nTxNewTime,
        const StakableCoin& stakeData,
        CMutableTransaction& txNew);

    StakableCoin FindProofOfStake(
        const CBlockIndex* chainTip,
        uint32_t blockBits,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime,
        bool& isVaultScript);

    void AppendBlockRewardPayoutsToTransaction(
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake);
    void SplitOrCombineUTXOS(
        const CAmount stakeSplit,
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake,
        const StakableCoin& stakeData,
        std::vector<const CTransaction*>& vwtxPrev);
public:
    PoSTransactionCreator(
        const Settings& settings,
        const CChainParams& chainParameters,
        const CChain& activeChain,
        const BlockMap& mapBlockIndex,
        const I_BlockSubsidyProvider& blockSubsidies,
        const I_BlockIncentivesPopulator& incentives,
        const I_ProofOfStakeGenerator& proofGenerator,
        CWallet& wallet,
        std::map<unsigned int, unsigned int>& hashedBlockTimestamps);
    ~PoSTransactionCreator();
    virtual bool CreateProofOfStake(
        const CBlockIndex* chainTip,
        uint32_t blockBits,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime);
};
#endif // COINSTAKE_CREATOR_H
