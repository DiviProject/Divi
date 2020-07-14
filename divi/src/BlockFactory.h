#ifndef BLOCK_FACTORY_H
#define BLOCK_FACTORY_H
#include <memory>
#include <boost/thread/recursive_mutex.hpp>
class CBlockTemplate;
class CScript;
class CWallet;
class CReserveKey;
class CBlock;
class CMutableTransaction;
class I_BlockTransactionCollector;
class I_CoinstakeCreator;
class CChain;
class CChainParams;
class CTxMemPool;
template <typename MutexObj>
class AnnotatedMixin;

class I_BlockFactory
{
public:
    virtual ~I_BlockFactory(){}
    virtual CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake) = 0;
};
class BlockFactory: public I_BlockFactory
{
private:
    CWallet& wallet_;
    int64_t& lastCoinstakeSearchInterval_;
    CChain& chain_;
    const CChainParams& chainParameters_;
    CTxMemPool& mempool_;
    AnnotatedMixin<boost::recursive_mutex>& mainCS_;

    std::shared_ptr<I_BlockTransactionCollector> blockTransactionCollector_;
    std::shared_ptr<I_CoinstakeCreator> coinstakeCreator_;
private:
    void SetRequiredWork(std::unique_ptr<CBlockTemplate>& pblocktemplate);
    void SetBlockTime(CBlock& block);
    void SetCoinbaseTransactionAndDefaultFees(
        std::unique_ptr<CBlockTemplate>& pblocktemplate, 
        const CMutableTransaction& coinbaseTransaction);
    void CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& coinbaseTx);
    bool AppendProofOfStakeToBlock(
        std::unique_ptr<CBlockTemplate>& pBlockTemplate);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        CWallet& wallet, 
        int64_t& lastCoinstakeSearchInterval,
        CChain& chain, 
        const CChainParams& chainParameters,
        CTxMemPool& mempool, 
        AnnotatedMixin<boost::recursive_mutex>& mainCS);
    virtual CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake);
};
#endif // BLOCK_FACTORY_H