#ifndef I_BLOCK_FACTORY_H
#define I_BLOCK_FACTORY_H
class CBlockTemplate;
class CScript;
class I_BlockFactory
{
public:
    virtual ~I_BlockFactory() = default;
    virtual CBlockTemplate* CreateNewPoWBlock(const CScript& scriptPubKey) = 0;
    virtual CBlockTemplate* CreateNewPoSBlock() = 0;
};
#endif// I_BLOCK_FACTORY_H