#ifndef EXTENDED_BLOCK_FACTORY_H
#define EXTENDED_BLOCK_FACTORY_H
#include <I_BlockFactory.h>
#include <memory>
#include <map>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>

class CBlock;
class BlockFactory;
class CTransaction;
class CChain;
class CChainParams;
class CBlockTemplate;
class CReserveKey;
class I_BlockSubsidyProvider;
class I_BlockTransactionCollector;
class Settings;
class I_StakingWallet;
class I_DifficultyAdjuster;
class I_BlockProofProver;

class ExtendedBlockFactory : public I_BlockFactory
{
private:
    std::vector<std::shared_ptr<CTransaction>> extraTransactions_;
    std::unique_ptr<CTransaction> customCoinstake_;
    std::unique_ptr<unsigned> blockBitsShift_;
    /** If set to true, then the generated block will not use any
     *  mempool transactions.  */
    bool ignoreMempool_;

    std::unique_ptr<I_BlockTransactionCollector> extendedTransactionCollector_;
    std::unique_ptr<I_BlockProofProver> blockProofProver_;
    std::unique_ptr<BlockFactory> blockFactory_;

    void VerifyBlockWithIsCompatibleWithCustomCoinstake(const CBlock& block);
public:
    ExtendedBlockFactory(
        const Settings& settings,
        const CChainParams& chainParameters,
        const CChain& chain,
        const I_DifficultyAdjuster& difficultyAdjuster,
        const I_BlockProofProver& blockProofProver,
        const I_StakingWallet& wallet,
        I_BlockTransactionCollector& blockTransactionCollector);
    ~ExtendedBlockFactory();

    CBlockTemplate* CreateNewPoWBlock(const CScript& scriptPubKey) override;
    CBlockTemplate* CreateNewPoSBlock() override;
    void addExtraTransaction(const CTransaction& tx);
    void setCustomCoinstake(const CTransaction& tx);
    void setIgnoreMempool(bool val);
    void setCustomBits(unsigned bits);
    void reset();
};
#endif// EXTENDED_BLOCK_FACTORY_H
