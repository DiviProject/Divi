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
    double initialCoinAgePerByteOfInputs;     //! Priority when entering the mempool
    unsigned int nHeight; //! Chain height when entering the mempool

    static inline double AllowFreeThreshold()
    {
        return COIN * 1440 / 250;
    }
public:
    static const unsigned int MEMPOOL_HEIGHT;
    CTxMemPoolEntry(const CTransaction& _tx, const CAmount& _nFee, int64_t _nTime, double _initialCoinAgeOfInputs, unsigned int _nHeight);
    CTxMemPoolEntry(const CTxMemPoolEntry& other);

    const CTransaction& GetTx() const { return this->tx; }
    double ComputeInputCoinAgePerByte(unsigned int currentHeight) const;
    CAmount GetFee() const { return nFee; }
    size_t GetTxSize() const { return nTxSize; }
    size_t GetModTxSize() const { return nModSize; }
    int64_t GetTime() const { return nTime; }
    unsigned int GetHeight() const { return nHeight; }

    static inline bool AllowFree(double coinAgeOfInputs)
    {
        // Large (in bytes) low-priority (new, small-coin) transactions
        // need a fee.
        return coinAgeOfInputs > AllowFreeThreshold();
    }
};
#endif// MEMPOOL_ENTRY_H