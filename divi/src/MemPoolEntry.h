#ifndef MEMPOOL_ENTRY_H
#define MEMPOOL_ENTRY_H
#include <primitives/transaction.h>
/**
 * CTxMemPool stores these:
 */
class CTxMemPoolEntry
{
private:
    CTransaction tx;
    CAmount nFee;         //! Cached to avoid expensive parent-transaction lookups
    size_t nTxSize;       //! ... and avoid recomputing tx size
    size_t nModSize;      //! ... and modified size for priority
    int64_t nTime;        //! Local time when entering the mempool
    double dPriority;     //! Priority when entering the mempool
    unsigned int nHeight; //! Chain height when entering the mempool

public:
    static const unsigned int MEMPOOL_HEIGHT;
    CTxMemPoolEntry(const CTransaction& _tx, const CAmount& _nFee, int64_t _nTime, double _dPriority, unsigned int _nHeight);
    CTxMemPoolEntry(const CTxMemPoolEntry& other);

    const CTransaction& GetTx() const { return this->tx; }
    double ComputeInputCoinAgePerByte(unsigned int currentHeight) const;
    CAmount GetFee() const { return nFee; }
    size_t GetTxSize() const { return nTxSize; }
    size_t GetModTxSize() const { return nModSize; }
    int64_t GetTime() const { return nTime; }
    unsigned int GetHeight() const { return nHeight; }
};
#endif// MEMPOOL_ENTRY_H