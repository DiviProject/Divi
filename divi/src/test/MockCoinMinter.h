#ifndef MOCK_COIN_MINTER_H
#define MOCK_COIN_MINTER_H
#include <gmock/gmock.h>
#include <I_CoinMinter.h>

class MockCoinMinter: public I_CoinMinter
{
public:
    MOCK_METHOD0(CanMintCoins, bool());
    MOCK_CONST_METHOD1(sleep, void(uint64_t));
    MOCK_METHOD1(setMintingRequestStatus,void(bool newStatus));
    MOCK_CONST_METHOD0(mintingHasBeenRequested, bool());
    MOCK_CONST_METHOD2(createNewBlock,bool(unsigned,bool));
};
#endif // MOCK_COIN_MINTER_H
