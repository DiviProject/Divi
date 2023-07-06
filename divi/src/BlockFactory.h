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
    const CChainParams& chainParameters_;
    const CChain& chain_;
    const I_DifficultyAdjuster& difficultyAdjuster_;
    const I_BlockProofProver& blockProofProver_;
    I_BlockTransactionCollector& blockTransactionCollector_;

    void SetBlockHeader(
        CBlockHeader& block,
        const CBlockIndex* pindexPrev) const;

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        const Settings& settings,
        const CChainParams& chainParameters,
        const CChain& chain,
        const I_DifficultyAdjuster& difficultyAdjuster,
        const I_BlockProofProver& blockProofProver,
        I_BlockTransactionCollector& blockTransactionCollector);

    CBlockTemplate* CreateNewPoWBlock(const CScript& scriptPubKey) override;
    CBlockTemplate* CreateNewPoSBlock() override;
};
#endif // BLOCK_FACTORY_H
