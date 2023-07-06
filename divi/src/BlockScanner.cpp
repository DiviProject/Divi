#include <BlockScanner.h>

#include <I_BlockDataReader.h>
#include <primitives/block.h>
#include <chain.h>

BlockScanner::BlockScanner(
    const I_BlockDataReader& blockReader,
    const CChain& activeChain,
    const CBlockIndex* startingBlock
    ): blockReader_(blockReader)
    , activeChain_(activeChain)
    , currentBlockIndex_(startingBlock)
    , currentBlock_(nullptr)
{
}
BlockScanner::~BlockScanner()
{
    currentBlock_.reset();
}

bool BlockScanner::readCurrentBlock()
{
    assert(currentBlock_);
    return currentBlockIndex_?blockReader_.ReadBlock(currentBlockIndex_,*currentBlock_):false;
}

bool BlockScanner::advanceToNextBlock()
{
    if(!currentBlock_)
    {
        currentBlock_.reset(new CBlock());
    }
    else
    {
        currentBlockIndex_ = currentBlockIndex_? activeChain_.Next(currentBlockIndex_) : nullptr;
    }
    return readCurrentBlock();
}

const TransactionVector& BlockScanner::blockTransactions() const
{
    assert(currentBlockIndex_);
    return currentBlock_->vtx;
}
const CBlock& BlockScanner::blockRef() const
{
    assert(currentBlockIndex_);
    return *currentBlock_;
}