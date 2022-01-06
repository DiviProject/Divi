#ifndef BLOCK_SCANNER_H
#define BLOCK_SCANNER_H
#include <vector>
#include <memory>
class CChain;
class CBlock;
class CBlockIndex;
class CTransaction;
class I_BlockDataReader;
using TransactionVector = std::vector<CTransaction>;
class BlockScanner
{
private:
    const I_BlockDataReader& blockReader_;
    const CChain& activeChain_;
    const CBlockIndex* currentBlockIndex_;
    std::unique_ptr<CBlock> currentBlock_;

    bool readCurrentBlock();
public:
    BlockScanner(const I_BlockDataReader& blockReader, const CChain& activeChain, const CBlockIndex* startingBlock);
    ~BlockScanner();
    bool advanceToNextBlock();
    const TransactionVector& blockTransactions() const;
    const CBlock& blockRef() const;
};
#endif //BLOCK_SCANNER_H