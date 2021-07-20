#ifndef I_KEYPOOL_RESERVER_H
#define I_KEYPOOL_RESERVER_H
#include <stdint.h>
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
#endif// I_KEYPOOL_RESERVER_H
