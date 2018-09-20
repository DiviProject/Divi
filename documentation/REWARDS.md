## Definition
__Block subsidity__ is a value in Divi which is created(minted) every new block. Rewards are sent to different parties(stakers, masternodes, treasury, etc). Will cover payments in next sections.
__Superblock__ is a special block which creates a payment to treasury or lottery or something else in perspective.

## Subsidity over the time

| Block height | Subsidity |
| ------------ | --------- |
| 0 - 1051200  | 1250 | 
| 1051200 - 2102400 | 1050 | 
| 2102400 - 3153600 | 850 | 
| ... | ... | 

Subsidity is decreasing every two years(1051200 blocks) by 200 divi, unless spork is not activated, which controls subsidity in explicit way.
Minimum block reward is 100 divi, so no matter how much time will pass minimum reward will be 100.

`static CAmount GetFullBlockValue(int nHeight)` in `main.cpp` is the place where full block reward is calculated.

## Ordinary Block Rewards

In every block there is one required payment it's the staker.
Staker will get reward based on current protocol setting, default value is 38%.

If there are masternodes that are ready to get paid, they will get 45% of the block.

16% are reserved for payment to the treasury

1% is reserved for payment to the charity fund

and 50 coins are reserved for lottery pool. 

All of those values can be changed by spork.

`CBlockRewards GetBlockSubsidity(int nHeight)` in `main.cpp`. is the place where block rewards are calculated.

## Superblocks

Keep in mind, that coins for superblock are 'reserved' in every block, the idea of superblock is to pay a large amount of coins in one UTXO comparing to generating coins in explicit way in every coinstake.

Superblocks are triggered on certain heights, currently we have two supersblocks:
1. Treasury & Charity payments.
2. Lottery

### Treasury & Charity


Treasury & Charity superblock is created based on variable `nTreasuryPaymentsCycle` in `chainparams.cpp`.
This superblock pays to charity and treasury in same block, but in different UTXOs.



```
static bool IsValidTreasuryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetTreasuryPaymentsStartBlock() &&
            ((nBlockHeight % Params().GetTreasuryPaymentsCycle()) == 0);
}
```

If it's appropriate time for treasury payment we calculate treasury payment for whole cycle and pay to treasury address. 

```
static int64_t GetTreasuryReward(const CBlockRewards &rewards)
{
    return rewards.nTreasuryReward * Params().GetTreasuryPaymentsCycle();
}
```

```
static int64_t GetCharityReward(const CBlockRewards &rewards)
{
    return rewards.nCharityReward * Params().GetTreasuryPaymentsCycle();
}
```

### Lottery

Lottery superblock is created based on variable `nLotteryBlockCycle` in `chainparams.cpp`.

```
static bool IsValidLotteryBlockHeight(int nBlockHeight)
{
    return nBlockHeight >= Params().GetLotteryBlockStartBlock() &&
            ((nBlockHeight % Params().GetLotteryBlockCycle()) == 0);
}
```

If it's appropriate time for lottery payment we calculate lottery pool in next way, and then make a payment.

```
static int64_t GetLotteryReward(const CBlockRewards &rewards)
{
    return Params().GetLotteryBlockCycle() * rewards.nLotteryReward;
}
```

