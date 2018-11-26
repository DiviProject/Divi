# Rewards

## Definition
__Block subsidity__ is a value in Divi which is created (minted) every new block. Rewards are sent to different parties (Stakers, Masternodes, Treasury, etc). Will cover payments in next sections.
__Superblock__ is a special block which creates a payment to treasury or lottery or something else in perspective.

## Subsidy over the time

| Block height | Subsidy |
| ------------ | --------- |
| 0 - 1051200  | 1250 | 
| 1051200 - 2102400 | 1050 | 
| 2102400 - 3153600 | 850 | 
| ... | ... | 

The subsidy decreases every two years (1051200 blocks) by 200 DIVI, unless spork is not activated, which explicitly controls subsidy.
Minimum block reward is 100 DIVI, so no matter how much time passes minimum reward will be 100.

`static CAmount GetFullBlockValue(int nHeight)` in `main.cpp` is the place where full block reward is calculated.

## Ordinary Block Rewards

In every block, there is one required payment. The Staker.
Stakers get rewarded based on current protocol setting; default value is 38%.

If there are masternodes that are ready to get paid, they get 45% of the block.

16% is reserved for payment to the treasury

1% is reserved for payment to the charity fund

50 coins are reserved for the lottery pool. 

All of those values can be changed by spork.

`CBlockRewards GetBlockSubsidity(int nHeight)` in `main.cpp` is the place where block rewards are calculated.

## Superblocks

Keep in mind, that coins for superblock are 'reserved' in every block, the idea of the superblock is to pay a large number of coins in one UTXO comparing to explicitly generating coins in every coinstake.

Superblocks are triggered by certain block heights. Currently, we have two superblocks:
1. Treasury & Charity payments.
2. Lottery

### Treasury & Charity


Treasury & Charity superblock is created based on variable `nTreasuryPaymentsCycle` in `chainparams.cpp`.
This superblock pays to charity and treasury in the same block, but in different UTXOs.



```cpp
static bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % Params().GetTreasuryPaymentsCycle()) == 0);
}
```

If it's the appropriate time for treasury payment, we calculate treasury payment for the whole cycle and pay to treasury address. 

```cpp
static int64_t GetTreasuryReward(const CBlockRewards &rewards)
{
    return rewards.nTreasuryReward * Params().GetTreasuryPaymentsCycle();
}
```

```cpp
static int64_t GetCharityReward(const CBlockRewards &rewards)
{
    return rewards.nCharityReward * Params().GetTreasuryPaymentsCycle();
}
```

### Lottery

Lottery superblock is created based on variable `nLotteryBlockCycle` in `chainparams.cpp`.

```cpp
static bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetLotteryBlockStartBlock() &&
            ((nBlockHeight % Params().GetLotteryBlockCycle()) == 0);
}
```

If it's the appropriate time for lottery payment we calculate lottery pool in the following way, and then make a payment.

```cpp
static int64_t GetLotteryReward(const CBlockRewards &rewards)
{
    return Params().GetLotteryBlockCycle() * rewards.nLotteryReward;
}
```