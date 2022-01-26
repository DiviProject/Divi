#include <test/FakeBlockIndexChain.h>
#include <chain.h>
#include <hash.h>
#include <blockmap.h>
#include <primitives/block.h>

namespace
{
namespace ChainExtensionHelpers
{
    CBlockIndex* extendFakeBlockIndexChain(
        CChain& currentChain,
        int heightAtTip,
        int32_t time,
        int32_t version)
    {
        CBlockIndex* pindex = nullptr;
        while(currentChain.Height() < heightAtTip)
        {
            pindex = new CBlockIndex();
            pindex->nHeight = currentChain.Height()+1;
            pindex->pprev = const_cast<CBlockIndex*>(pindex->nHeight > 0 ? currentChain.Tip(): nullptr);
            pindex->nTime = time;
            pindex->nVersion = version;
            pindex->BuildSkip();
            currentChain.SetTip(pindex);
        }
        return pindex;
    }
    CBlockIndex* extendTo(
        CChain& currentChain,
        int heightAtTip,
        int32_t time,
        int32_t version)
    {
        return ChainExtensionHelpers::extendFakeBlockIndexChain(currentChain,heightAtTip,time,version);
    }
    CBlockIndex* extendBy(
        CChain& currentChain,
        unsigned additionalBlocks,
        int32_t time,
        int32_t version)
    {
        int heightAtTip = currentChain.Height() + additionalBlocks;
        return ChainExtensionHelpers::extendTo(currentChain,heightAtTip,time,version);
    }

    CBlockIndex* attachNewBlock(CChain& currentChain, const CBlock& block)
    {
        assert(currentChain.Tip());
        CBlockIndex* pindex = new CBlockIndex(block);
        pindex->nHeight = currentChain.Height()+1;
        pindex->pprev = const_cast<CBlockIndex*>(currentChain.Tip());
        pindex->BuildSkip();
        currentChain.SetTip(pindex);
        return pindex;
    }

} // namespace ChainExtensionHelpers
} // anonymous namespace


void FakeBlockIndexChain::resetFakeChain()
{
    for(CBlockIndex* ptr: fakeChain)
    {
        if(ptr) delete ptr;
    }
    fakeChain.clear();
}


FakeBlockIndexChain::FakeBlockIndexChain(): fakeChain()
{
}
FakeBlockIndexChain::~FakeBlockIndexChain()
{
    resetFakeChain();
}

void FakeBlockIndexChain::extendTo(
    unsigned heightAtTip,
    int32_t time,
    int32_t version)
{
    fakeChain.reserve(heightAtTip+1);
    extendFakeBlockIndexChain(heightAtTip+1,time,version,fakeChain);
}
void FakeBlockIndexChain::extendBy(
    unsigned additionalBlocks,
    int32_t time,
    int32_t version)
{
    unsigned heightAtTip = fakeChain.size()+additionalBlocks - 1;
    extendTo(heightAtTip,time,version);
}

void FakeBlockIndexChain::extendFakeBlockIndexChain(
    unsigned totalNumberOfBlocks,
    int32_t time,
    int32_t version,
    std::vector<CBlockIndex*>& currentChain
    )
{
    while(currentChain.size() < totalNumberOfBlocks)
    {
        CBlockIndex* pindex = new CBlockIndex();
        pindex->nHeight = currentChain.size();
        pindex->pprev = currentChain.size() > 0 ? currentChain.back(): nullptr;
        pindex->nTime = time;
        pindex->nVersion = version;
        pindex->BuildSkip();
        currentChain.push_back(pindex);
    }
}

void FakeBlockIndexChain::attachNewBlock(const CBlock& block)
{
    assert(fakeChain.back());
    CBlockIndex* chainTip = fakeChain.back();
    CBlockIndex* pindex = new CBlockIndex(block);
    pindex->nHeight = fakeChain.size();
    pindex->pprev = chainTip;
    pindex->BuildSkip();
    fakeChain.push_back(pindex);
}


CBlockIndex* FakeBlockIndexChain::at(unsigned height) const
{
    return fakeChain[height];
}

 CBlockIndex* FakeBlockIndexChain::Tip() const
 {
     return fakeChain.empty()? NULL: fakeChain.back();
 }

 void FakeBlockIndexChain::pruneToHeight(unsigned int height)
 {
     if(fakeChain.size()>height+1)
     {
         fakeChain.resize(height+1);
     }
 }

 // FakeBlockIndexChainWithHashes
FakeBlockIndexWithHashes::FakeBlockIndexWithHashes(
    unsigned numberOfBlocks,
    unsigned blockStartTime,
    unsigned versionNumber
    ): randomBlockHashSeed_(uint256S("135bd924226929c2f4267f5e5c653d2a4ae0018187588dc1f016ceffe525fad2"))
    , blockIndexByHash(new BlockMap())
    , activeChain(new CChain())
{
    addBlocks(numberOfBlocks, versionNumber, blockStartTime);
}
FakeBlockIndexWithHashes::~FakeBlockIndexWithHashes()
{
    activeChain.reset();
    BlockMap& blockIndices = *blockIndexByHash;
    for(auto& hashAndIndexPair: blockIndices)
    {
        if(hashAndIndexPair.second)
        {
            delete hashAndIndexPair.second;
            hashAndIndexPair.second = nullptr;
        }
    }
    blockIndexByHash.reset();
}

void FakeBlockIndexWithHashes::extendChainBlocks(
    const CBlockIndex* chainToExtend,
    unsigned numberOfBlocks,
    unsigned versionNumber,
    unsigned blockStartTime)
{
    unsigned startingBlockHeight = 0u;
    if(chainToExtend)
    {
        startingBlockHeight = chainToExtend->nHeight+1;
        blockStartTime = activeChain->operator[](0)->GetBlockTime();
        if(activeChain->Tip()!=chainToExtend)
        {
            activeChain->SetTip(chainToExtend);
            assert(activeChain->Tip()==chainToExtend);
        }
    }

    for(unsigned blockHeight = startingBlockHeight; blockHeight < numberOfBlocks+startingBlockHeight; ++blockHeight)
    {
        auto* pindex = ChainExtensionHelpers::extendBy(*activeChain,1,blockStartTime+60*blockHeight,versionNumber);
        CHashWriter hasher(SER_GETHASH,0);
        hasher << randomBlockHashSeed_++ << blockHeight;
        const auto it = blockIndexByHash->emplace(hasher.GetHash(), pindex).first;
        pindex->phashBlock = &(it->first);
    }
}

void FakeBlockIndexWithHashes::addBlocks(
    unsigned numberOfBlocks,
    unsigned versionNumber,
    unsigned blockStartTime)
{
    extendChainBlocks(activeChain->Tip(),numberOfBlocks,versionNumber,blockStartTime);
}

void FakeBlockIndexWithHashes::fork(
    unsigned numberOfBlocks,
    unsigned ancestorDepth)
{
    const CBlockIndex* chainTip = activeChain->Tip();
    assert(chainTip);
    const CBlockIndex* chainToExtend = chainTip->GetAncestor(chainTip->nHeight - ancestorDepth);
    assert(chainToExtend);
    extendChainBlocks(chainToExtend,numberOfBlocks,chainToExtend->nVersion,chainToExtend->nTime);
    assert((activeChain->Tip()->nHeight - chainToExtend->nHeight) == static_cast<int>(numberOfBlocks) );
}

void FakeBlockIndexWithHashes::addSingleBlock(CBlock& block)
{
    const CBlockIndex* chainTip = activeChain->Tip();
    assert(chainTip);
    block.nTime = chainTip->GetBlockTime()+60;
    block.nVersion = chainTip->nVersion;

    block.hashMerkleRoot = block.BuildMerkleTree();
    assert(chainTip->phashBlock);
    block.hashPrevBlock = chainTip->GetBlockHash();
    auto* pindexNewTip = ChainExtensionHelpers::attachNewBlock(*activeChain,block);
    chainTip = activeChain->Tip();

    const auto it = blockIndexByHash->emplace(block.GetHash(), pindexNewTip).first;
    pindexNewTip->phashBlock = &(it->first);
}
