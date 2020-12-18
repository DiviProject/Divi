#ifndef RESERVE_KEY_H
#define RESERVE_KEY_H
#include <stdint.h>
#include <pubkey.h>
class CKeyPool;
/** A key allocated from the key pool. */


// Assuming wallet now inherits from I_KeypoolReserver ...
class I_KeypoolReserver
{
public:
    virtual void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fInternal) = 0;
    virtual void ReturnKey(int64_t nIndex, bool fInternal) = 0;
    virtual void KeepKey(int64_t nIndex) = 0;
};

class CReserveKey
{
private:
    I_KeypoolReserver& keypoolReserver_;
protected:
    int64_t nIndex;
    CPubKey vchPubKey;
    bool fInternal;
public:
    CReserveKey(I_KeypoolReserver& keypoolReserver);
    ~CReserveKey();

    void ReturnKey();
    bool GetReservedKey(CPubKey &pubkey, bool fInternalIn);
    void KeepKey();
};
#endif // RESERVE_KEY_H