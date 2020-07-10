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
    void SetRequiredWork(CBlock& block);
    void SetBlockTime(CBlock& block);
    void SetCoinbaseTransactionAndDefaultFees(
        std::unique_ptr<CBlockTemplate>& pblocktemplate, 
        const CMutableTransaction& coinbaseTransaction);
    void CreateCoinbaseTransaction(const CScript& scriptPubKeyIn, CMutableTransaction& coinbaseTx);
    bool AppendProofOfStakeToBlock(
        CWallet& pwallet, 
        CBlock& block);

    CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake);
public:
    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake);
};
#endif // BLOCK_FACTORY_H