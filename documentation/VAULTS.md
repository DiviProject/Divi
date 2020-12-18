# Staking Vaults (available since >=2.0.0)

## Definition
Staking Vaults are a nifty little gadget that allows users to delegate the staking of their funds to an exclusive third party in a trustless way. By leveraging the programmability of blockchains, it creates a cold spending key (i.e. __owner key__) and a hot staking key (i.e. __manager key__) that allows users to secure the network while also securing the funds they stake.

## How it works

### General
When a minter creates a proof-of-stake block, they do three things:
1. Solve a hashing problem (á la proof-of-work) that gets easier the larger the amount staked,
2. Spend the staked funds (to prove that you have indeed control over them),
3. Pay themselves back the staked funds + the staking reward.

Steps (2) and (3) happen in the same transaction. This transaction is included as the second transaction in a block. The transaction thus created is specially marked as a `coinstake` transaction and there has to be exactly one in each proof-of-stake block or it the block is rejected.
Staking Vaults are formed from two keys. A spending key (that may be in a cold wallet) and a staking key thats in a hot wallet. The spending key is needed for anything other than staking. The staking key is needed by staking and can only be used for that. The way that this is enforced is by requiring any spending attempt by the staking key to force the transaction to be a `coinstake`.

Finally in order to ensure the funds are safe at all times, spending funds from a vault using a coinstake transaction will enforce the holder of the staking key to pay the required funds back (as per step (3)) to the vault address itself - otherwise the block having this tx is invalidated. This results in a closed loop when staking with a vault.

### Command Line endpoints
The endpoints can be found in the `rpcwallet.cpp`.
1. `./divi-cli fundvault [owner_address:]manager_address amount [comment] [comment-to]`
Sends funds to a staking vault address. These are DIFFERENT from normal addresses. These funds are now said to be 'vaulted' and you can no longer stake them.
2. `./divi-cli reclaimvaultfunds destination amount`
Sends funds from your currently vaulted funds to a destination address of your choice. The change address is one of your own.
2. `./divi-cli addvault <owner_address>:<manager_address> funding_txhash`
Vault managers must decide to manually accept responsibility to stake on another's behalf
3. `./divi-cli removevault <owner_address>:<manager_address>`
Vault managers can choose to rescind their responsibility to stake on anothers behalf.
4. `./divi-cli getcoinavailability [verbose]`
Shows amounts totals in the: Vaulted, Stakable and Spendable categories of funds. When verbose is set to 'true' it further shows txhashes and vault addresses.

## Some Use Cases
1. Two people who know each other to stake on the same node without giving up ultimate control of their funds.
2. A user can delegate the staking of their funds to a remotely hosted node without hot-wallet concerns, because the spending key is decoupled from the staking key. Virtually 'cold staking'

## Additional technical details
See `StakingVaultScript.h/cpp`, and `bool CheckCoinstakeForVaults(const CTransaction& tx, const CBlockRewards& expectedRewards, const CCoinsViewCache& view)` for additional details on the script logic