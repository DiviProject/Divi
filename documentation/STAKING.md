# Staking

## Definition
Divi's equivalent to Bitcoin’s Proof of Work is Proof of Stake, and instead of Bitcoin’s mining, Divi has staking. Rather than needing vast amounts of computer power to secure the network,  “Coin Age" enables Divi's security protocols. We find the Coin Age by multiplying the number of coins by the “age” of these coins, i.e. the amount of time between now and the last transaction using said coins.

Generally speaking, PoS is solving the same hashing problem as PoW, but hashes are built from Coin Age and existing UTXOs (Unspent Transaction Outputs) not from running a mining rig.

## Unspent Transaction Output (UTXO)

An Unspent Transaction Output is a transaction hash, located in a wallet that contains unspent funds. Each time funds from a UTXO leave the wallet for any reason (in the case of a transaction, for example), a new UTXO is created containing the remaining funds.

## How staking works
Staking is the process your wallet uses to validate transactions and award you with coins. When your wallet is staking, it is checking transactions to make sure everyone who sends coins was,in fact, the owner of those coins and had the right to transfer them. If most of the wallets online agree that a transaction is valid, then it gets accepted by the network.

As a reward for keeping the network secure, every minute one online wallet is chosen to receive a stake reward based on the coins they own.

Each online wallet tries to create a stake, by scanning through its UTXOs and trying to match PoS difficulty with their weight. We calculate weight by multiplying Coin Age * amount of UTXO. The more coins you have and the older they are, the higher chance you have to create a stake.

It's required to be online during staking because otherwise, you are not able to sync to peers.

## Coin Age
Coin Age is a property of each UTXO; it's time that has passed from the moment of UTXO creation. 

### Example
Imagine today is Monday, and I receive 1000 DIVI from Alice. On Tuesday, that UTXO has a Coin Age of 24 hours. Now, let's say I want to send 200 DIVI to Bob, but since I am sending my 1000 Divi UTXO, my Coin Age resets, and I get a new UTXO that is worth 800 DIVI.

The minimum Coin Age that is allowed for staking is 1 hour, while the maximum Coin Age is 7 days, which means that if your UTXO is older than 7 days, it counts as if it were 7 days old.

Only moving funds from one UTXO to another can reset the Coin Age. Only transactions can reset the Coin Age, and there are no other ways to reset this value.

## Coin maturity
Every time you successfully stake, you get a reward for keeping the network secure. When you create a stake, you spend your UTXO with value X to reset the Coin Age, and in the same transaction, you get X + Reward to the same address, which creates a brand new UTXO and thereby resets your Coin Age.

To solidify network stability and to ensure that spending of a recently earned reward that may not have been 100% included into the blockchain, staking rewards (coins created by staking transaction) are unspendable for 20 blocks. These unspendable coins are considered to be "immature."

### Example
My wallet creates a stake with 10 000 DIVI, (essentially spending 10 000 DIVI) getting back 10 000 + 456 DIVI, at this point 10 456 coins are "immature" for 20 blocks, only after that am I able to spend the coins that were used to create the Coin Stake.