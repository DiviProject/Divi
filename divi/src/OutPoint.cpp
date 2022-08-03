#include <OutPoint.h>

#include <tinyformat.h>
#include <hash.h>
#include <utilstrencodings.h>

COutPoint::COutPoint() { SetNull(); }
COutPoint::COutPoint(const uint256& hashIn, uint32_t nIn)
  : hash(hashIn), n(nIn)
{}

void COutPoint::SetNull() { hash.SetNull(); n = (uint32_t) -1; }
bool COutPoint::IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }
bool operator<(const COutPoint& a, const COutPoint& b)
{
    return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
}

bool operator==(const COutPoint& a, const COutPoint& b)
{
    return (a.hash == b.hash && a.n == b.n);
}

bool operator!=(const COutPoint& a, const COutPoint& b)
{
    return !(a == b);
}

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString()/*.substr(0,10)*/, n);
}

std::string COutPoint::ToStringShort() const
{
    return strprintf("%s-%u", hash.ToString().substr(0,64), n);
}

uint256 COutPoint::GetHash() const
{
    return Hash(BEGIN(hash), END(hash), BEGIN(n), END(n));
}