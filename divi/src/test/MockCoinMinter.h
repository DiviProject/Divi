#ifndef MOCK_COIN_MINTER_H
#define MOCK_COIN_MINTER_H
#include <gmock/gmock.h>
#include <I_CoinMinter.h>

class MockCoinMinter: public I_CoinMinter
{
public:
    MOCK_METHOD0(isMintable, bool());
    MOCK_CONST_METHOD0(satisfiesMintingRequirements, bool());
    MOCK_CONST_METHOD0(limitStakingSpeed, bool());
    MOCK_CONST_METHOD0(isAtProofOfStakeHeight, bool());
    MOCK_CONST_METHOD1(sleep, void(uint64_t));
    MOCK_METHOD1(setMintingRequestStatus,void(bool newStatus));
    MOCK_CONST_METHOD0(mintingHasBeenRequested, bool());
    MOCK_CONST_METHOD3(createNewBlock,bool(unsigned,CReserveKey&,bool));
};
#endif // MOCK_COIN_MINTER_H