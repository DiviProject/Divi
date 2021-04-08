#ifndef BLOCK_FACTORY_H
#define BLOCK_FACTORY_H
#include <memory>
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
class Settings;

class BlockFactory: public I_BlockFactory
{
private:
    const Settings& settings_;
    const CChain& chain_;
    const CChainParams& chainParameters_;

    I_BlockTransactionCollector& blockTransactionCollector_;
    I_PoSTransactionCreator& coinstakeCreator_;

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
        I_PoSTransactionCreator& coinstakeCreator,
        const Settings& settings,
        const CChain& chain,
        const CChainParams& chainParameters);

    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake) override;
};
#endif // BLOCK_FACTORY_H
