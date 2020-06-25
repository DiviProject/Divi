#include <test_only.h>

#include <MockCoinMinter.h>
#include <miner.h>
#include <wallet.h>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::_;

BOOST_AUTO_TEST_SUITE(CoinMintingTests)

BOOST_AUTO_TEST_CASE(willSuccesfullyTransitionFromNonMintableToMintableInProofOfStakeMode)
{
    NiceMock<MockCoinMinter> minter;

    bool mintableReturnValue = false;
    ON_CALL(minter, CanMintCoins())
        .WillByDefault(
            Invoke(
                [&mintableReturnValue]()->bool
                {
                    return mintableReturnValue;
                }
            )
        );
    ON_CALL(minter, mintingHasBeenRequested())
        .WillByDefault(
            Invoke(
                []()->bool
                {
                    static unsigned actualCalls = 0;
                    static unsigned maxMintingHasBeenRequestedCalls = 10;
                    if(actualCalls < maxMintingHasBeenRequestedCalls)
                    {
                        ++actualCalls;
                        return true;
                    }
                    return false;
                }
            )
        );
    ON_CALL(minter, sleep(_))
        .WillByDefault(
            Invoke(
                [&mintableReturnValue](uint64_t)->void
                {
                    mintableReturnValue = true;
                }
            )
        );

    EXPECT_CALL(minter, sleep(_)).Times(1);
    EXPECT_CALL(minter, createNewBlock(_,_,_)).Times(9);
    
    bool mintableStatus = false;
    bool proofOfStake = true;
    unsigned int extraNonce = 0u;
    std::shared_ptr<CWallet> testWallet(new CWallet());
    CReserveKey reserveKey(testWallet.get());
    
    MintCoins(
        mintableStatus,
        proofOfStake, 
        minter, 
        extraNonce,
        reserveKey);
}

BOOST_AUTO_TEST_SUITE_END()