#ifndef MOCK_SUPERBLOCK_HEIGHT_VALIDATOR_H
#define MOCK_SUPERBLOCK_HEIGHT_VALIDATOR_H
#include <I_SuperblockHeightValidator.h>

#include <gmock/gmock.h>

class MockSuperblockHeightValidator: public I_SuperblockHeightValidator
{
public:
    MOCK_CONST_METHOD1(GetTreasuryBlockPaymentCycle, int(int nBlockHeight));
    MOCK_CONST_METHOD1(GetLotteryBlockPaymentCycle, int(int nBlockHeight));
    MOCK_CONST_METHOD1(IsValidLotteryBlockHeight, bool(int nBlockHeight));
    MOCK_CONST_METHOD1(IsValidTreasuryBlockHeight, bool(int nBlockHeight));
};
#endif // MOCK_SUPERBLOCK_HEIGHT_VALIDATOR_H