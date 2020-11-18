#ifndef BLOCK_FACTORY_H
#define BLOCK_FACTORY_H
#include <memory>
#include <boost/thread/recursive_mutex.hpp>
#include <map>
#include <I_BlockFactory.h>
class CBlockTemplate;
class CScript;
class CWallet;
class CReserveKey;
class CBlock;
class CMutableTransaction;
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class CChain;
class CChainParams;
class CTxMemPool;
template <typename MutexObj>
class AnnotatedMixin;

class BlockFactory: public I_BlockFactory
{
private:
    CChain& chain_;
    const CChainParams& chainParameters_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;

    std::shared_ptr<I_BlockTransactionCollector> blockTransactionCollector_;
    std::shared_ptr<I_PoSTransactionCreator> coinstakeCreator_;

    void SetRequiredWork(CBlockTemplate& pblocktemplate);
    void SetBlockTime(CBlock& block);
    void SetCoinbaseTransactionAndDefaultFees(
        CBlockTemplate& pblocktemplate,
        const CMutableTransaction& coinbaseTransaction);
    void CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& coinbaseTx);
    bool AppendProofOfStakeToBlock(
        CBlockTemplate& pBlockTemplate);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        I_BlockTransactionCollector& blockTransactionCollector,
        CWallet& wallet,
        int64_t& lastCoinstakeSearchInterval,
        std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
        CChain& chain,
        const CChainParams& chainParameters,
        CTxMemPool& mempool,
        AnnotatedMixin<boost::recursive_mutex>& mainCS);

    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake) override;
};
#endif // BLOCK_FACTORY_H
