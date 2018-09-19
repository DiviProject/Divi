## Definition

__Lottery__ in Divi is a process of selecting 11 winners in pseudo-random way in the end of every lottery cycle. Lottery winners are determined in every block, this is important to understand, this is not like real-world lottery where random numbers are generated.

__Lottery reward__ is amount of coins that is generated in every block and is added to lottery pool.

__Lottery cycle__ is how many blocks needs to be minted to trigger new lottery.

## How does it work

### General 

Let's pickup random values to show this on example:

```
LC = 200 blocks
LR = 50 divi
Total lottery pool = LC * LR = 200 * 50 = 10000 divi
```

Let's pretend that we are on block 200, it's end of last lottery cycle and start of new lottery cycle. Algorithm will be next:

1. In every block we calculate score using hash of coinstake and hash of last lottery. 
2. After calculating score we save it in `CBlockIndex` together with 11 other winners. 
3. In the end of the lottery cycle, block 400 in this example, minter needs to pay to the winners of the lottery. 

Any peer looking on the blockchain can do same math and tell if lottery winner is valid or no, this is important, peers are able to reach consensus in decentralized way looking on their local blockchains. 

### What is score? How to calculate it?

Score is`SHA256(hash_of_coinstake + hashOfLastLotteryBlock)`

```
static uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock)
{
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hashCoinbaseTx << hashLastLotteryBlock;
    return ss.GetHash();
}
```

### What happens when new block is generated?
When new block is generated we calculate score for this block, compare it with 11(or less) scores that are already saved and add it to the list in case it's bigger then minimum score. New winners list is saved in `vLotteryWinnersCoinstakes` in `CBlockIndex`

## Lottery participation 
To participate in lottery all you need is to stake with UTXO that is > 10000 Divi. This value is configured by multivalue spork. 

## Lottery winners
Current model supports 11 winners, biggest hash gets half of the pool, and other half is distributed by other 10 winners in even manner. 

## Where to look at?
Everything related lottery is in file `masternode-payments.cpp`.