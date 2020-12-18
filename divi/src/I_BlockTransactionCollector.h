#ifndef I_BLOCK_TRANSACTION_COLLECTOR_H
#define I_BLOCK_TRANSACTION_COLLECTOR_H
class CBlockTemplate;

class I_BlockTransactionCollector
{
public:
    virtual ~I_BlockTransactionCollector(){}
    virtual bool CollectTransactionsIntoBlock (
        CBlockTemplate& pblocktemplate) const = 0;
};
#endif// I_BLOCK_TRANSACTION_COLLECTOR_H