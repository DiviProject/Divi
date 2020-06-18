#ifndef BLOCK_FACTORY_H
#define BLOCK_FACTORY_H
class CBlockTemplate;
class CScript;
class CWallet;
class CReserveKey;

class BlockFactory
{
private:
    CBlockTemplate* createNewBlock(const CScript& scriptPubKeyIn, bool fProofOfStake);
public:
    CBlockTemplate* createNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake);
};
#endif // BLOCK_FACTORY_H