#include <MemPoolEntry.h>

#include <serialize.h>
#include <FeeAndPriorityCalculator.h>
#include <version.h>

const unsigned int CTxMemPoolEntry::MEMPOOL_HEIGHT = 0x7FFFFFFF;

CTxMemPoolEntry::CTxMemPoolEntry(
    const CTransaction& _tx,
    const CAmount& _nFee,
    int64_t _nTime,
    double _dPriority,
    unsigned int _nHeight
    ) : tx(_tx)
    , nFee(_nFee)
    , nTxSize(0u)
    , nModSize(0u)
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

double CTxMemPoolEntry::ComputeInputCoinAgePerByte(unsigned int currentHeight) const
{
    CAmount nValueIn = tx.GetValueOut() + nFee;
    double deltaPriority = nModSize > 0u? ((double)(currentHeight - nHeight) * nValueIn) / nModSize : 0.0;
    double dResult = dPriority + deltaPriority;
    return dResult;
}