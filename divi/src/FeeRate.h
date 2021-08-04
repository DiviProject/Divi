#ifndef FEE_RATE_H
#define FEE_RATE_H
#include "amount.h"
/** Type-safe wrapper class to for fee rates
 * (how much to pay based on transaction size)
 */
#include "serialize.h"
class CFeeRate
{
private:
    CAmount nSatoshisPerK; // unit is satoshis-per-1,000-bytes
    mutable CAmount maxTransactionFee;
public:
    static const unsigned FEE_INCREMENT_STEPSIZE;
    CFeeRate();
    explicit CFeeRate(const CAmount& _nSatoshisPerK);
    CFeeRate(const CAmount& nFeePaid, size_t nSize);
    CFeeRate(const CFeeRate& other);

    CAmount GetFee(size_t size) const;                  // unit returned is satoshis
    CAmount GetFeePerK() const { return GetFee(1000); } // satoshis-per-1000-bytes
    const CAmount& GetMaxTxFee() const;
    void SetMaxFee(CAmount maximumTxFee);

    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nSatoshisPerK < b.nSatoshisPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nSatoshisPerK > b.nSatoshisPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nSatoshisPerK == b.nSatoshisPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nSatoshisPerK <= b.nSatoshisPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nSatoshisPerK >= b.nSatoshisPerK; }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nSatoshisPerK);
    }
};
#endif // FEE_RATE_H