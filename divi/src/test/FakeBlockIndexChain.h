#ifndef FAKE_BLOCK_INDEX_CHAIN_H
#define FAKE_BLOCK_INDEX_CHAIN_H
#include <cstdint>
#include <vector>
#include <memory>
#include <uint256.h>
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
    CBlockIndex* Tip() const;
};

class BlockMap;
class CChain;
class FakeBlockIndexWithHashes
{
private:
    uint256 randomBlockHashSeed_;
    unsigned numberOfBlocks_;
    FakeBlockIndexChain fakeBlockIndexChain_;
public:
    std::unique_ptr<BlockMap> blockIndexByHash;
    std::unique_ptr<CChain> activeChain;
    FakeBlockIndexWithHashes(
        unsigned numberOfBlocks,
        unsigned blockStartTime,
        unsigned versionNumber);
    ~FakeBlockIndexWithHashes();

    void addBlocks(
        unsigned numberOfBlocks,
        unsigned versionNumber,
        unsigned blockStartTime = 0);
};
#endif //FAKE_BLOCK_INDEX_CHAIN_H