#ifndef I_CHAIN_TIP_MANAGER_H
#define I_CHAIN_TIP_MANAGER_H
class CBlock;
class CBlockIndex;

class I_ChainTipManager
{
public:
    virtual ~I_ChainTipManager(){}
    virtual bool connectTip(const CBlock* block, CBlockIndex* blockIndex) const = 0;
    virtual bool disconnectTip() const = 0;
};
#endif// I_CHAIN_TIP_MANAGER_H