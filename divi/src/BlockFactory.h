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
class I_BlockSubsidyProvider;
class I_BlockTransactionCollector;
class I_PoSTransactionCreator;
class CChain;
class CChainParams;
class Settings;
class CBlockIndex;
class CBlockHeader;
class I_DifficultyAdjuster;

class BlockFactory: public I_BlockFactory
{
private:
    const Settings& settings_;
    const CChain& chain_;
    const CChainParams& chainParameters_;
    const I_DifficultyAdjuster& difficultyAdjuster_;

    const I_BlockSubsidyProvider& blockSubsidies_;
    I_BlockTransactionCollector& blockTransactionCollector_;
    I_PoSTransactionCreator& coinstakeCreator_;

    bool AppendProofOfStakeToBlock(
        CBlockTemplate& pBlockTemplate);

    void UpdateTime(
        CBlockHeader& block,
        const CBlockIndex* pindexPrev) const;
    void SetBlockHeader(
        CBlockHeader& block,
        const CBlockIndex* pindexPrev) const;
    bool AppendProofOfWorkToBlock(
        CBlockTemplate& blocktemplate);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        const I_BlockSubsidyProvider& blockSubsidies,
        const I_DifficultyAdjuster& difficultyAdjuster,
        I_BlockTransactionCollector& blockTransactionCollector,
        I_PoSTransactionCreator& coinstakeCreator,
        const Settings& settings,
        const CChain& chain,
        const CChainParams& chainParameters);

    CBlockTemplate* CreateNewPoWBlock(const CScript& scriptPubKey) override;
    CBlockTemplate* CreateNewPoSBlock() override;
};
#endif // BLOCK_FACTORY_H
