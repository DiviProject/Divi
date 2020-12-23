#include <BlockIndexLotteryUpdater.h>

#include <LotteryCoinstakes.h>
#include <chainparams.h>
#include <chain.h>
#include <SuperblockSubsidyContainer.h>
#include <LotteryWinnersCalculator.h>

BlockIndexLotteryUpdater::BlockIndexLotteryUpdater(
    const CChainParams& chainParameters,
    const CChain& activeChain,
    const CSporkManager& sporkManager
    ): chainParameters_(chainParameters)
    , subsidyContainer_(new SuperblockSubsidyContainer(chainParameters_))
    , lotteryCalculator_(new LotteryWinnersCalculator(chainParameters_.GetLotteryBlockStartBlock(),activeChain,sporkManager, subsidyContainer_->superblockHeightValidator()) )
{
}

BlockIndexLotteryUpdater::~BlockIndexLotteryUpdater()
{
    lotteryCalculator_.reset();
    subsidyContainer_.reset();
}

void BlockIndexLotteryUpdater::UpdateBlockIndexLotteryWinners(const CBlock &block, CBlockIndex *newestBlockIndex) const
{
    static LotteryCoinstakeData emptyData;
    const int nHeight = newestBlockIndex->nHeight;
    const CBlockIndex *prevBlockIndex = newestBlockIndex->pprev;

    const LotteryCoinstakeData& previousBlockLotteryCoinstakeData = prevBlockIndex? prevBlockIndex->vLotteryWinnersCoinstakes : emptyData;
    const CTransaction& coinMintingTransaction  = (nHeight > chainParameters_.LAST_POW_BLOCK() )? block.vtx[1] : block.vtx[0];
    newestBlockIndex->vLotteryWinnersCoinstakes = lotteryCalculator_->CalculateUpdatedLotteryWinners(coinMintingTransaction,previousBlockLotteryCoinstakeData,nHeight);
}