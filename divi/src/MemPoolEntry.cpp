#include <MemPoolEntry.h>

#include <serialize.h>
#include <version.h>

const unsigned int CTxMemPoolEntry::MEMPOOL_HEIGHT = 0x7FFFFFFF;

static unsigned int CalculateModifiedSize(const CTransaction& tx, unsigned int nTxSize)
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    for (std::vector<CTxIn>::const_iterator it(tx.vin.begin()); it != tx.vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}

CTxMemPoolEntry::CTxMemPoolEntry(
    const CTransaction& _tx,
    const CAmount& _nFee,
    int64_t _nTime,
    double _initialCoinAgeOfInputs,
    unsigned int _nHeight
    ) : tx(_tx)
    , nFee(_nFee)
    , nTxSize(0u)
    , nModSize(0u)
    , nTime(_nTime)
    , initialCoinAgePerByteOfInputs(0.0)
    , nHeight(_nHeight)
{
    nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    nModSize = CalculateModifiedSize(tx,nTxSize);
    initialCoinAgePerByteOfInputs = nModSize? _initialCoinAgeOfInputs/ nModSize: 0.0;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry& other)
{
    *this = other;
}

double CTxMemPoolEntry::ComputeInputCoinAgePerByte(unsigned int currentHeight) const
{
    CAmount nValueIn = tx.GetValueOut() + nFee;
    double deltaCoinAgePerByteOfInputs = nModSize > 0u? ((double)(currentHeight - nHeight) * nValueIn) / nModSize : 0.0;
    return initialCoinAgePerByteOfInputs + deltaCoinAgePerByteOfInputs;
}