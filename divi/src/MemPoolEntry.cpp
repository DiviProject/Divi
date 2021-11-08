#include <MemPoolEntry.h>

#include <serialize.h>
#include <FeeAndPriorityCalculator.h>
#include <version.h>

const unsigned int CTxMemPoolEntry::MEMPOOL_HEIGHT = 0x7FFFFFFF;

CTxMemPoolEntry::CTxMemPoolEntry() : nFee(0), nTxSize(0), nModSize(0), nTime(0), dPriority(0.0)
{
    nHeight = CTxMemPoolEntry::MEMPOOL_HEIGHT;
}

CTxMemPoolEntry::CTxMemPoolEntry(
    const CTransaction& _tx,
    const CAmount& _nFee,
    int64_t _nTime,
    double _dPriority,
    unsigned int _nHeight
    ) : tx(_tx)
    , nFee(_nFee)
    , nTime(_nTime)
    , dPriority(_dPriority)
    , nHeight(_nHeight)
{
    nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    nModSize = FeeAndPriorityCalculator::instance().CalculateModifiedSize(tx,nTxSize);
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry& other)
{
    *this = other;
}

double CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    CAmount nValueIn = tx.GetValueOut() + nFee;
    double deltaPriority = ((double)(currentHeight - nHeight) * nValueIn) / nModSize;
    double dResult = dPriority + deltaPriority;
    return dResult;
}