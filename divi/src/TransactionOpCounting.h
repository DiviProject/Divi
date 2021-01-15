#ifndef TRANSACTION_OP_COUNTING_H
#define TRANSACTION_OP_COUNTING_H
class CTransaction;
class CCoinsViewCache;
unsigned int GetLegacySigOpCount(const CTransaction& tx);
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& mapInputs);
#endif// TRANSACTION_OP_COUNTING_H