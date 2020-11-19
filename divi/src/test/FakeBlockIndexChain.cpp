#include <test/FakeBlockIndexChain.h>
#include <chain.h>
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

CBlockIndex* FakeBlockIndexChain::at(unsigned height) const
{
    return fakeChain[height];
}

 CBlockIndex* FakeBlockIndexChain::tip() const
 {
     return fakeChain.empty()? NULL: fakeChain.back();
 }