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
class BlockIncentivesPopulator;
class CChain;
class I_PoSStakeModifierService;
class ProofOfStakeGenerator;
class I_BlockSubsidyProvider;
class BlockMap;
class StakedCoins;
struct StakableCoin;

class PoSTransactionCreator: public I_PoSTransactionCreator
{
private:
    const CChainParams& chainParameters_;
    CChain& activeChain_;
    const BlockMap& mapBlockIndex_;
    const I_BlockSubsidyProvider& blockSubsidies_;
    const BlockIncentivesPopulator& incentives_;
    std::unique_ptr<ProofOfStakeGenerator> proofGenerator_;
    std::unique_ptr<StakedCoins> stakedCoins_;
    CWallet& wallet_;
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps_;
    int64_t hashproofTimestampMinimumValue_;

    void CombineUtxos(
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
        unsigned int& nTxNewTime);

    void AppendBlockRewardPayoutsToTransaction(
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake);
    void SplitOrCombineUTXOS(
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake,
        const StakableCoin& stakeData,
        std::vector<const CTransaction*>& vwtxPrev);
public:
    PoSTransactionCreator(
        const CChainParams& chainParameters,
        CChain& activeChain,
        const BlockMap& mapBlockIndex,
        const I_PoSStakeModifierService& stakeModifierService,
        const I_BlockSubsidyProvider& blockSubsidies,
        const BlockIncentivesPopulator& incentives,
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