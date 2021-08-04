#include <FakeMerkleTxConfirmationNumberCalculator.h>

#include <chain.h>
#include <blockmap.h>
#include <merkletx.h>
#include <chainparams.h>

FakeMerkleTxConfirmationNumberCalculator::FakeMerkleTxConfirmationNumberCalculator(
    const CChain& activeChain,
    const BlockMap& blockIndices
    ): activeChain_(activeChain)
    , blockIndices_(blockIndices)
    , coinbaseMaturity_(Params(CBaseChainParams::UNITTEST).COINBASE_MATURITY())
{
}

std::pair<const CBlockIndex*,int> FakeMerkleTxConfirmationNumberCalculator::FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const
{
    auto it = blockIndices_.find(merkleTx.hashBlock);
    if(it == blockIndices_.end() || !activeChain_.Contains(it->second))
    {
        return {nullptr,0};
    }
    else
    {
        return std::make_pair(it->second,activeChain_.Height() - it->second->nHeight+1);
    }
}
int FakeMerkleTxConfirmationNumberCalculator::GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const
{
    return FindConfirmedBlockIndexAndDepth(merkleTx).second;
}
int FakeMerkleTxConfirmationNumberCalculator::GetBlocksToMaturity(const CMerkleTx& merkleTx) const
{
    if (!(merkleTx.IsCoinBase() || merkleTx.IsCoinStake()))
        return 0;
    return std::max(0, (coinbaseMaturity_ + 1) - GetNumberOfBlockConfirmations(merkleTx));
}