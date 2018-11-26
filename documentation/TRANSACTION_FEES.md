## Definition

__Transaction fee__ is amount which is paid for transaction to be processed. 

### Fee calculation

Divi uses flexible approach depending on transaction amount, size and global network modifiers. 

We use next formula to calculate fees: ` Fee = (TV * TVM) * TS / TSM `

In this formula: 

`TV:` Transaction value in DIVI

`TS:` Transaction size in bytes

`TVM:` Transaction value multiplier - global value for network

`TSM:` Transaction size multiplier - global value for network 

### Global transaction fee modifiers

To be as flexible as possible we introduce TVM and TSM which are values that are selected using spork. Changing those values allows us to change the fee for whole network. 

Default values are: 

`TVM = 0.0001`

`TSM = 300`

### Minimum possible fee

Minimum possible fee that will be processed by network(though with lower priority) is `Fee = 10000 satoshis per KB`

### Maximum possible fee

Maximum fee is controlled by spork, default maximum fee is 100 DIVI. 

### Fee deduction

Fee is paid automatically and is calculated as `Fee = (TxIn - TxOut)`, so fee will be paid in spendable outputs(inputs) of the transaction. 

### Burning

Divi uses PoS tail emission which means coins are generated always, no matter how long it runs in contrast to bitcoin where the supply is limited. To control the inflation we burn fees, this way no one gets the fee, and with every transaction fee which was paid gets burnt reducing total coin supply. 