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
class CWalletTx;
class CChainParams;
class I_SuperblockSubsidyContainer;
class BlockIncentivesPopulator;
class CChain;
class I_PoSStakeModifierService;
class ProofOfStakeGenerator;
class I_BlockSubsidyProvider;
class BlockMap;
class StakedCoins;

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
        const CAmount& allowedStakingAmount,
        CMutableTransaction& txNew,
        CAmount& nCredit,
        std::vector<const CWalletTx*>& walletTransactions);

    bool SetSuportedStakingScript(
        const std::pair<const CWalletTx*, unsigned int>& transactionAndIndexPair,
        CMutableTransaction& txNew);

    bool SelectCoins(CAmount allowedStakingBalance);

    bool FindHashproof(
        const CBlockIndex* chainTip,
        unsigned int nBits,
        unsigned int& nTxNewTime,
        const std::pair<const CWalletTx*, unsigned int>& stakeData,
        CMutableTransaction& txNew);

    std::pair<const CWalletTx*, CAmount> FindProofOfStake(
        const CBlockIndex* chainTip,
        uint32_t blockBits,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime);

    void AppendBlockRewardPayoutsToTransaction(
        const CBlockIndex* chainTip,
        CMutableTransaction& txCoinStake,
        CAmount allowedStakingAmount,
        const std::pair<const CWalletTx*, CAmount>& stakeData,
        std::vector<const CWalletTx*>& vwtxPrev);
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