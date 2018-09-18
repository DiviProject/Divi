## Definition
Sporks are a feature that was implemented by Dash, it allows to execute a change to consensus protocol in centralized manner. 
Dash and PIVX support very simple sproks that work as binary switches. They are defined in next way:

Spork is defined as `SPORK_ID` and `SPORK_VALUE` so spork is actually a pair.

Usualy `SPORK_VALUE` is spork activation time. It is used in code in next way: 
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
Let's take a look on example, we need to be able to change block value to arbitrary one. We can't achive it with binary switches, because for binary switches we need to have all options compiled into code. We can only switch between two 'branches'.
### Chained spork or Multivalue spork or Spork with history
Multivalue spork is an extended spork which is `SPORK_ID` and `[SPORK_DATA, SPORK_DATA, ...]`. It is used to change consensus with arbitary values starting from some block height.
It contains all historic values that were applied to this spork. 
### Format of Multivalue spork
Let's take a look on SPORK_15_BLOCK_VALUE, it's a spork that changes block value
```
SPORK_ID = 10012
SPORK_VALUES = ["1000;100", "800;200", "600;400"]
```
It means that there are 3 historic values:
1000 coins starting from block 100,
800 coins starting from block 200,
600 coins starting from block 400.

