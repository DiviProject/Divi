#ifndef BLOCK_INDEX_LOTTERY_UPDATER_H
#define BLOCK_INDEX_LOTTERY_UPDATER_H
#include <memory>
class CBlock;
class CBlockIndex;
class LotteryWinnersCalculator;
class CSporkManager;
class CChain;
class CChainParams;
class SuperblockSubsidyContainer;

class BlockIndexLotteryUpdater
{
private:
    const CChainParams& chainParameters_;
    std::unique_ptr<SuperblockSubsidyContainer> subsidyContainer_;
    std::unique_ptr<LotteryWinnersCalculator> lotteryCalculator_;
public:
    BlockIndexLotteryUpdater(
        const CChainParams& chainParameters,
        const CChain& activeChain,
        const CSporkManager& sporkManager);
    ~BlockIndexLotteryUpdater();
    void UpdateBlockIndexLotteryWinners(const CBlock &block, CBlockIndex *newestBlockIndex) const;
};
#endif// BLOCK_INDEX_LOTTERY_UPDATER_H