#ifndef SUPERBLOCK_HEIGHT_VALIDATOR_H
#define SUPERBLOCK_HEIGHT_VALIDATOR_H
#include <I_SuperblockHeightValidator.h>
class CChainParams;
class SuperblockHeightValidator: public I_SuperblockHeightValidator
{
private:
    const CChainParams& chainParameters_;
    int transitionHeight_;
    int superblockCycleLength_;

public:
    SuperblockHeightValidator(const CChainParams& chainParameters);
    int getTransitionHeight() const;
    const CChainParams& getChainParameters() const;

    virtual int GetTreasuryBlockPaymentCycle(int nBlockHeight) const;
    virtual int GetLotteryBlockPaymentCycle(int nBlockHeight) const;
    virtual bool IsValidLotteryBlockHeight(int nBlockHeight) const;
    virtual bool IsValidTreasuryBlockHeight(int nBlockHeight) const;
};
#endif // SUPERBLOCK_HEIGHT_VALIDATOR_H