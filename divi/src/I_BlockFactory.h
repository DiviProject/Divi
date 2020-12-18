#ifndef I_BLOCK_FACTORY_H
#define I_BLOCK_FACTORY_H
class CBlockTemplate;
class CReserveKey;
class I_BlockFactory
{
public:
    virtual ~I_BlockFactory() = default;
    virtual CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake) = 0;
};
#endif// I_BLOCK_FACTORY_H