#ifndef EXTENDED_BLOCK_FACTORY_H
#define EXTENDED_BLOCK_FACTORY_H
#include <I_BlockFactory.h>
#include <memory>
#include <map>
#include <vector>
#include <boost/thread/recursive_mutex.hpp>

class BlockFactory;
class CTransaction;
class CChain;
class CChainParams;
class CBlockTemplate;
class CReserveKey;
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class Settings;

class ExtendedBlockFactory : public I_BlockFactory
{
private:
    std::unique_ptr<BlockFactory> blockFactory_;
    std::vector<std::shared_ptr<CTransaction>> extraTransactions_;
    std::unique_ptr<CTransaction> customCoinstake_;

    /** If set to true, then the generated block will not use any
     *  mempool transactions.  */
    bool ignoreMempool_ = false;

public:
    ExtendedBlockFactory(
        I_BlockTransactionCollector& blockTransactionCollector,
        I_PoSTransactionCreator& coinstakeCreator,
        const Settings& settings,
        const CChain& chain,
        const CChainParams& chainParameters);
    ~ExtendedBlockFactory();

    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reserveKey, bool fProofOfStake) override;
    void addExtraTransaction(const CTransaction& tx);
    void setCustomCoinstake(const CTransaction& tx);
    void setIgnoreMempool(bool val);
};
#endif// EXTENDED_BLOCK_FACTORY_H
