#ifndef FAKE_BLOCK_INDEX_CHAIN_H
#define FAKE_BLOCK_INDEX_CHAIN_H
#include <cstdint>
#include <vector>
#include <memory>
#include <uint256.h>
class CBlockIndex;
class CBlock;
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
    void attachNewBlock(const CBlock& block);
    void pruneToHeight(unsigned int height);
};

class BlockMap;
class CChain;
class FakeBlockIndexWithHashes
{
private:
    uint256 randomBlockHashSeed_;

    void extendChainBlocks(
        const CBlockIndex* chainToExtend,
        unsigned numberOfBlocks,
        unsigned versionNumber,
        unsigned blockStartTime);
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

    void fork(
        unsigned numberOfBlocks,
        unsigned ancestorDepth=1);

    void addSingleBlock(CBlock& block);
};
#endif //FAKE_BLOCK_INDEX_CHAIN_H
