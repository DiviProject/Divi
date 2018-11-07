# Lottery Blocks

## Definition

__Lottery__ in Divi is a process of selecting 11 winners in a pseudo-random way at the end of every lottery cycle. Lottery winners are determined in every block, this is important to understand, this is not like the real-world lottery where random numbers are generated.

__Lottery reward__ is the number of coins that are generated in every block and is added to the lottery pool.

__Lottery cycle__ is how many blocks need to be minted to trigger new lottery.

## How it works

### General 

Let's pickup random values to show this on example:

```cpp
LC = 200 blocks
LR = 50 divi
Total lottery pool = LC * LR = 200 * 50 = 10000 divi
```

Let's pretend that we are on block 200, and it's the end of last lottery cycle and the start of new lottery cycle. The algorithm behaves like this:

1. In every block, we calculate the score using the hash of coin stake and the hash of the last lottery. 
2. After calculating the score, we save it in `CBlockIndex` together with 11 other winners. 
3. At the end of the lottery cycle, block 400 in this example, minter needs to pay to the winners of the lottery. 

Any peer looking on the blockchain can do the same math and tell if lottery winner is valid or no, this is important, peers can reach consensus in a decentralized way looking on their local blockchains. 

### What is the 'score' and how is it calculated?

Score is`SHA256(hash_of_coinstake + hashOfLastLotteryBlock)`

```cpp
static uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock)
{
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hashCoinbaseTx << hashLastLotteryBlock;
    return ss.GetHash();
}
```

### What happens when a new block is generated?
When a new block is generated we calculate the score for this block, compare it with 11(or less) scores that are already saved and add it to the list in case it's bigger than minimum score. The new winners list is saved in `vLotteryWinnersCoinstakes` in `CBlockIndex`

## Lottery participation 
To participate in the lottery, all you need is to stake with UTXO that is > 10000 Divi. This value can be updated by DVS (multivalue sporks.) 

## Lottery winners
The Current model supports 11 winners.  The biggest hash gets half of the pool, and the other 10 winners are distributed another 5% each. 

## Where to look?
Everything related to the lottery is in file `masternode-payments.cpp`.