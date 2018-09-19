## Definition
Sporks are a feature that was implemented by Dash, it allows to execute a change to consensus protocol in centralized manner. 
Dash and PIVX support very simple sporks that work as binary switches. They are defined in next way:

Spork is defined as `SPORK_ID` and `SPORK_VALUE` so spork is actually a pair.

Usually `SPORK_VALUE` is spork activation time. It is used in code in next way: 
```
bool SporkManager::IsSporkActive(int sporkID)
{
  // ... misc code that gets spork value
  
    return SPORK_VALUE < GetCurrenTime();
}
```

and it will be used in code in next way:

```
  if (sporkManager.IsSporkActive (SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
      // do something in case spork is active
      nMasternode_Age = GetAdjustedTime() - mn.sigTime;
      if ((nMasternode_Age) < nMasternode_Min_Age) {
          continue; // Skip masternodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
      }
  }
        
```

Also value can be any integer, like block height or some other constant.

## Multivalue spork
Divi has a need to make changes to the blockchain in more complex way comparing to binary switches.
### Motivation
We need to be able to save all previous values that were used with this spork. Why? 
Let's take a look on example, we need to be able to change block value to arbitrary one. We can't achieve it with binary switches, because for binary switches we need to have all options compiled into code. We can only switch between two 'branches'.
### Chained spork or Multivalue spork or Spork with history
Multivalue spork is an extended spork which is `SPORK_ID` and `[SPORK_DATA, SPORK_DATA, ...]`. It is used to change consensus with arbitrary values starting from some block height.
It contains all historic values that were applied to this spork. 
### Format of Multivalue spork
Let's take a look on `SPORK_15_BLOCK_VALUE`, it's a spork that changes block value
```
SPORK_ID = 10012
SPORK_VALUES = ["1000;100", "800;200", "600;400"]
```
It means that there are 3 historic values:
1000 coins starting from block 100,
800 coins starting from block 200,
600 coins starting from block 400.

## How to activate sporks

Spork can needs to be signed by private key that was used to create spork address which is hardcoded into protocol. 

1. Start `divid` with `-sporkkey=private_key`. This will allow you to send sporks to the network.
2. Use RPC call `spork "SPORK_15_BLOCK_VALUE" "1000;100"`. Actual format depends on spork.

**__ATTENTION YOU NEED TO BE EXTREMELY CAREFUL WITH MULTIVALUE SPORKS, DON'T EVER APPLY MULTIVALUE SPORK WITH BLOCK HEIGHT THAT HAS ALREADY PASSED, IT MAY FORK THE CHAIN.__**

## How to check sporks state

Use RPC call `spork show` to see current state of sporks.

## Changes to P2P protocol for new spork system

In Dash or PIVX sporks are synced as part of second layer(Masternodes), before syncing masternodes and masternode-payments client requests sporks from his peers and then proceeded with masternode sync. 

Divi supports different model, since we need to know about all sporks before syncing to properly react to any consensus changes. 

Standard protocol is:

1. Peer A creates connection to peer B.
2. Peer A sends `version` message
3. Peer B receives `version` message, sends `verack` and `sporkcount` messages to peer A.
4. Peer A receives `verack` and is suspeneded till the moment he syncs all sporks using INV mechanism. 
5. At this state Peer A can send messages to peer B and process messages from him, doing classic sync.

## Format for different spork values that are supported by Divi

| Spork ID | Spork Value | Example |
| -------- | ----------- | ------- |
| SPORK_2_SWIFTTX_ENABLED | Activation time | 0 |
| SPORK_3_SWIFTTX_BLOCK_FILTERING | Activation time | 0 | 
| SPORK_5_INSTANTSEND_MAX_VALUE | Integer, value in coins | 1000 |  
| SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT | Activation time | 4070908800 | 
| SPORK_9_SUPERBLOCKS_ENABLED | Activation time | 4070908800 | 
| SPORK_10_MASTERNODE_PAY_UPDATED_NODES | Activation time | 4070908800 | 
| SPORK_12_RECONSIDER_BLOCKS | Integer, number of blocks to reconsider | 0 | 
| SPORK_13_BLOCK_PAYMENTS | Integers(Percentage), format: `stakeReward;mnReward;treasuryReward;proposalsReward;charityReward;blockHeightActivation` | 40;20;20;0;20;1000 |
| SPORK_14_TX_FEE | Integers, format: `txValueMultiplier;txSizeMultiplier;maxFee;nMinFeePerKb;blockHeightActivation` | 1000;300;100;10000;600 | 
| SPORK_15_BLOCK_VALUE | Integer(value in coins) format: `blockValue;blockHeightActivation` | 1200;200 |
| SPORK_16_LOTTERY_TICKET_MIN_VALUE | Integer(value in coins) format: `minLotteryValue;blockHeightActivation` | 10000;500 |  

