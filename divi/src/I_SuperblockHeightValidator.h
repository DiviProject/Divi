#ifndef I_SUPERBLOCK_HEIGHT_VALIDATOR_H
#define I_SUPERBLOCK_HEIGHT_VALIDATOR_H
class I_SuperblockHeightValidator
{
public:
    virtual ~I_SuperblockHeightValidator(){}
    virtual int GetTreasuryBlockPaymentCycle(int nBlockHeight) const = 0;
    virtual int GetLotteryBlockPaymentCycle(int nBlockHeight) const = 0;
    virtual bool IsValidLotteryBlockHeight(int nBlockHeight) const = 0;
    virtual bool IsValidTreasuryBlockHeight(int nBlockHeight) const = 0;
};
#endif // I_SUPERBLOCK_HEIGHT_VALIDATOR_H