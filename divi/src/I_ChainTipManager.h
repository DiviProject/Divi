#ifndef I_CHAIN_TIP_MANAGER_H
#define I_CHAIN_TIP_MANAGER_H
class CBlock;
class CBlockIndex;
class CValidationState;
class I_ChainTipManager
{
public:
    virtual ~I_ChainTipManager(){}
    virtual bool connectTip(CValidationState& state, const CBlock* block, CBlockIndex* blockIndex,const bool defaultBlockChecking) const = 0;
    virtual bool disconnectTip(CValidationState& state, const bool updateCoinDatabaseOnly) const = 0;
};
#endif// I_CHAIN_TIP_MANAGER_H