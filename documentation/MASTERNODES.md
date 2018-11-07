## Definition 

__Masternodes__ are computers that run a divi wallet and perform utility functions for network such as storing metadata and processing special transactions.

__Masternode collateral__ amount that is needed to run a masternodes, each masternode wallet needs lock certain amount of divi in his wallet in order to register as masternode in the network. 

__Masternode tier__ is a type of masternode, there are 5 types of masternodes(Copper, Silver, Gold, Platinum, Diamond) depending on type you will owner will get more rewards and provide different services to the network.

### Masternode rewards

Every masternode forms a second layer of network which enables extra features to the blockchain. Masternodes are paid for performing those functions from block rewards. Default breakout is:


| Type | Reward (%) |
| -------- | ----------- |
| Staking | 38 |
| Masternode | 45 |
| Treasury | 16 |
| Proposals | 0 | 
| Charity | 1 | 

Payments layout can be changed by spork in any way.


### Payment logic

Masternode payments in Divi are determined using a decentralized random selection algorithm based on masternode level. Every masternode appears in the global list. Once masternode is active for some amount of time it will be eligible for payments. Once eligible it takes part in probabilistic process that determines winner for next block. Different levels have different chances to win.

### Winner selection

When new block is added to network every masternode submits vote for winner which will appear in 10 blocks in future. Voting is probabilistic process of selecting masternode winner. 

__Score__ is a double SHA256 of the funding transaction hash and ticket index for all masternodes in the selection pool. Score is compared with the block hash 100 blocks ago. The masternode with the closest numeric hash value to that block hash is selected for payment. 

__Ticket__ is a number that represents one try to create a score. 

Whoever gets maximum hash wins the selection process. Each masternode tries to produce a maximum allowed score(hash) based on their masternode level. On practice it means that it will try several times to hash in order to maximize chances of winning.  

We build a pool of tickets for every masternode and select masternode with closest numeric hash value to block hash.

### Tickets

Masternode has different chances to win depending on their level:

| Level | Number of tickets | 
| ----- | ----------------- |
| Copper | 20 | 
| Silver | 63 | 
| Gold | 220 | 
| Platinum | 690 | 
| Diamond | 2400 | 

On practice it means that Platinum node will produce 690 scores(hashes) and select one score that maximizes chances for winning. 

Code that calculates scores can be found: `masternode.cpp:212`


