#include <test/FakeBlockIndexChain.h>
#include <chain.h>
#include <hash.h>
#include <blockmap.h>
#include <primitives/block.h>

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

 // FakeBlockIndexChainWithHashes
FakeBlockIndexWithHashes::FakeBlockIndexWithHashes(
    unsigned numberOfBlocks,
    unsigned blockStartTime,
    unsigned versionNumber
    ): randomBlockHashSeed_(uint256S("135bd924226929c2f4267f5e5c653d2a4ae0018187588dc1f016ceffe525fad2"))
    , numberOfBlocks_(numberOfBlocks)
    , fakeBlockIndexChain_()
    , blockIndexByHash(new BlockMap())
    , activeChain(new CChain())
{
    addBlocks(numberOfBlocks_,versionNumber,blockStartTime);
}
FakeBlockIndexWithHashes::~FakeBlockIndexWithHashes()
{
    activeChain.reset();
    blockIndexByHash.reset();
}

void FakeBlockIndexWithHashes::addBlocks(
    unsigned numberOfBlocks,
    unsigned versionNumber,
    unsigned blockStartTime)
{
    unsigned startingBlockHeight = 0u;
    const CBlockIndex* chainTip = fakeBlockIndexChain_.Tip();
    if(chainTip)
    {
        startingBlockHeight = chainTip->nHeight+1;
        blockStartTime = fakeBlockIndexChain_.at(0)->GetBlockTime();
    }

    for(unsigned blockHeight = startingBlockHeight; blockHeight < numberOfBlocks+startingBlockHeight; ++blockHeight)
    {
        fakeBlockIndexChain_.extendBy(1,blockStartTime+60*blockHeight,versionNumber);
        CHashWriter hasher(SER_GETHASH,0);
        hasher << randomBlockHashSeed_ << blockHeight;
        BlockMap::iterator it = blockIndexByHash->insert(std::make_pair(hasher.GetHash(), fakeBlockIndexChain_.Tip() )).first;
        fakeBlockIndexChain_.Tip()->phashBlock = &(it->first);
    }
    activeChain->SetTip(fakeBlockIndexChain_.Tip());
}

void FakeBlockIndexWithHashes::addSingleBlock(CBlock& block)
{
    CBlockIndex* chainTip = activeChain->Tip();
    assert(chainTip);
    block.nTime = chainTip->GetBlockTime()+60;
    block.nVersion = chainTip->nVersion;

    block.hashMerkleRoot = block.BuildMerkleTree();
    assert(chainTip->phashBlock);
    block.hashPrevBlock = chainTip->GetBlockHash();
    fakeBlockIndexChain_.attachNewBlock(block);
    chainTip = fakeBlockIndexChain_.Tip();

    BlockMap::iterator it = blockIndexByHash->insert(std::make_pair(block.GetHash(), chainTip )).first;
    chainTip->phashBlock = &(it->first);
    activeChain->SetTip(chainTip);
}