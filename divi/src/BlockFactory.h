#ifndef BLOCK_FACTORY_H
#define BLOCK_FACTORY_H
#include <memory>
class CBlockTemplate;
class CScript;
class CWallet;
class CReserveKey;
class CBlock;
class CMutableTransaction;

class BlockFactory
{
private:
    CWallet& wallet_;
    int64_t& lastCoinstakeSearchInterval_;
private:
    void SetRequiredWork(CBlock& block);
    void SetBlockTime(CBlock& block);
    void SetCoinbaseTransactionAndDefaultFees(
        std::unique_ptr<CBlockTemplate>& pblocktemplate, 
        const CMutableTransaction& coinbaseTransaction);
    void CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& coinbaseTx);
    bool AppendProofOfStakeToBlock(
        CBlock& block);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    BlockFactory(CWallet& wallet, int64_t& lastCoinstakeSearchInterval);
    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake);
};
#endif // BLOCK_FACTORY_H