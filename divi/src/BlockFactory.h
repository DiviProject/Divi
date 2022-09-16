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
class I_BlockProofProver;

class BlockFactory final: public I_BlockFactory
{
private:
    const Settings& settings_;
    const CChain& chain_;
    const CChainParams& chainParameters_;
    const I_DifficultyAdjuster& difficultyAdjuster_;

    I_BlockTransactionCollector& blockTransactionCollector_;
    const I_BlockProofProver& blockProofProver_;

    void SetBlockHeader(
        CBlockHeader& block,
        const CBlockIndex* pindexPrev) const;

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        const I_DifficultyAdjuster& difficultyAdjuster,
        I_BlockTransactionCollector& blockTransactionCollector,
        const I_BlockProofProver& blockProofProver,
        const Settings& settings,
        const CChain& chain,
        const CChainParams& chainParameters);

    CBlockTemplate* CreateNewPoWBlock(const CScript& scriptPubKey) override;
    CBlockTemplate* CreateNewPoSBlock() override;
};
#endif // BLOCK_FACTORY_H
