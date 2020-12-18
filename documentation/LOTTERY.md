# Lottery Blocks

## Definition

__Lottery__ in Divi is a process of selecting 11 winners in a pseudo-random way at the end of every lottery cycle. Lottery winners are determined in every block, this is important to understand, this is not like the real-world lottery where random numbers are generated.

__Lottery Amount__ is the number of coins that are generated in every block and is added to the lottery pool.

__Lottery Cycle__ is how many blocks need to be minted to trigger new lottery.

## How it works

### General

Let's pickup random values to show this on example:

```cpp
LotteryCycle = 200 blocks
LotteryAmount = 50 divi
Total lottery pool = LotteryCycle * LotteryAmount = 200 * 50 DIVI = 10000 DIVI
```

Let's pretend that we are on block 200, and it's the end of last lottery cycle and the start of new lottery cycle. The algorithm behaves like this:

1. In every block, we calculate the score using the hash of coin stake and the hash of the last lottery.
2. After calculating the score, we save it in `CBlockIndex` together with 11 other winners.
3. At the end of the lottery cycle, block 200 in this example, minter needs to pay to the winners of the lottery.

Any peer looking on the blockchain can do the same math and tell if lottery winner is valid or no, this is important, peers can reach consensus in a decentralized way looking on their local blockchains.

### What is the 'score' and how is it calculated?

Score is`SHA256(hash_of_coinstake || hashOfLastLotteryBlock)`

```cpp
uint256 LotteryWinnersCalculator::CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock)
{
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hashCoinbaseTx << hashLastLotteryBlock;
    return ss.GetHash();
}
```

### What happens when a new block is generated?
When a new block is generated we calculate the score for the proof of stake in this block, compare it the top 11 (there may be less than 11)
scores that are already saved in a list and add it to the list in case it's bigger than minimum score. The new winners list is saved in `vLotteryWinnersCoinstakes` in `CBlockIndex`.

## Lottery participation
To participate in the lottery, all you need is to stake with UTXO that is > 10000 Divi. This value can be updated by DVS (multivalue sporks.)

### Addendum for version >=2.0.0
In order to make lottery blocks for uniform, winning on a given lottery block takes the winning address out of consideration for the next four lottery cycles.

## Lottery winners
The Current model supports 11 winners.  The biggest hash gets half of the pool, and the other 10 winners are distributed another 5% each.

## Where to look?
Everything related to the lottery winner calculation `LotteryWinnersCalculator.h/cpp` and in `BlockIncentivesPopulator.h/cpp` for the payout of block rewards including the lottery.