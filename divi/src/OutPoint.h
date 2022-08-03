#ifndef OUTPOINT_H
#define OUTPOINT_H
#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"

#include <list>

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    COutPoint();
    COutPoint(const uint256& hashIn, uint32_t nIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull();
    bool IsNull() const;
    friend bool operator<(const COutPoint& a, const COutPoint& b);
    friend bool operator==(const COutPoint& a, const COutPoint& b);
    friend bool operator!=(const COutPoint& a, const COutPoint& b);
    std::string ToString() const;
    std::string ToStringShort() const;

    uint256 GetHash() const;

};
#endif // OUTPOINT_H