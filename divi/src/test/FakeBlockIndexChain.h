#ifndef FAKE_BLOCK_INDEX_CHAIN_H
#define FAKE_BLOCK_INDEX_CHAIN_H
#include <cstdint>
#include <vector>
class CBlockIndex;
struct FakeBlockIndexChain
{
private:
    std::vector<CBlockIndex*> fakeChain;
public:
    void resetFakeChain();
    FakeBlockIndexChain();
    ~FakeBlockIndexChain();
    void extendTo(
        unsigned heightAtTip,
        int32_t time,
        int32_t version);
    void extendBy(
        unsigned additionalBlocks,
        int32_t time,
        int32_t version);
    static void extendFakeBlockIndexChain(
        unsigned totalNumberOfBlocks,
        int32_t time,
        int32_t version,
        std::vector<CBlockIndex*>& currentChain
        );
    CBlockIndex* at(unsigned int) const;
    CBlockIndex* tip() const;
};
#endif //FAKE_BLOCK_INDEX_CHAIN_H