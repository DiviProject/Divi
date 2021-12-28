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

class BlockFactory: public I_BlockFactory
{
private:
    const Settings& settings_;
    const CChain& chain_;
    const CChainParams& chainParameters_;

    const I_BlockSubsidyProvider& blockSubsidies_;
    I_BlockTransactionCollector& blockTransactionCollector_;
    I_PoSTransactionCreator& coinstakeCreator_;

    void SetRequiredWork(CBlockTemplate& pblocktemplate) const;
    void SetBlockTime(CBlock& block) const;
    bool AppendProofOfStakeToBlock(
        CBlockTemplate& pBlockTemplate);

    void UpdateTime(
        CBlockHeader& block,
        const CBlockIndex* pindexPrev) const;
    void SetBlockHeaders(
        CBlockTemplate& pblocktemplate,
        const bool& proofOfStake) const;
    void FinalizeBlock (
        CBlockTemplate& pblocktemplate,
        const bool& fProofOfStake) const;
    bool AppendProofOfWorkToBlock(
        CBlockTemplate& blocktemplate);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(
        const I_BlockSubsidyProvider& blockSubsidies,
        I_BlockTransactionCollector& blockTransactionCollector,
        I_PoSTransactionCreator& coinstakeCreator,
        const Settings& settings,
        const CChain& chain,
        const CChainParams& chainParameters);

    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake) override;
};
#endif // BLOCK_FACTORY_H
