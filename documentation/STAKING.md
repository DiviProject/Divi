## Definition

Divi's equivalent to Bitcoin’s Proof of Work is Proof of Stake, and instead of Bitcoin’s mining, Divi has staking. Rather than needing vast amounts of computer power to secure the network, Divi's security is based on “coin age”. Coin age is calculated by multiplying the number of coins by the “age” of these coins i.e. the amount of time since they were last transacted. 

Generally talking PoS is solving same hashing problem as PoW but hashes are built from coin age and existing UTXOs not from running minning rig. 

### How staking happens

Staking is the process your wallet uses to validate transactions and award you with coins. When your wallet is staking, it is checking transactions to make sure everyone who sends coins actually owned those coins and had the right to transfer them. If most of the wallets online agree that a transaction is valid, then it gets accepted by the network.

As a reward for keeping the network secure, every minute one online wallet is chosen to receive a stake reward on the coins they own. 

Each online wallet tries to create a stake, by scanning through it's own UTXOs, and trying to match PoS difficulty with their weight. Weight is calculated as: `coin age * amount of UTXO`. The more coins you have and the older they are, the higher chance you will have to create a stake. 

It's required to be online during staking, because otherwise you are not able to sync to peers. 


### Coin age

Coin age is a property of UTXO, it's basically time that has passed from the moment that UTXO was created. Imagine today is Monday, I got 1000 Divi from Alice, on Tuesday I will have 24 hours coin age on that UTXO. Let's say I want to spend 200 Divi. I send it to Bob, but since I am spending my 1000 Divi UTXO, my coin age is reset since I get new UTXO which is 800 Divi worth. 

Minimum coin age that is allowed for staking is: 1 hour, maximum coin age is 7 days, it means that if your UTXO is older than 7 days, it will count only as 7 days.

Only moving funds from one UTXO to another can spend the coin age. Only transactions can reset the coin age, no other means to do that. 

### Coin maturity

Every time you successfully stake, you get a reward for keeping network secure. The reward is sent in interesting way. When you create a stake you spend your UTXO with value `X` to reset the coin age, and in same transaction you get `X + reward` to the same address this way creating new UTXO. So it's basically spending your coin age. 

To keep the network stable and to solve spending of reward that wasn't 100% included into the blockchain coins that were created in stake transaction are unspendable for 20 blocks. Those coins are called immature. 

Example: we create a stake with `1000 Divi`, we spend `1000 Divi`, getting back `1000 + 250 Divi`, at this point 1250 coins are immature for 20 blocks, only after that you will be able to spend your coins that were used to create the coinstake. 