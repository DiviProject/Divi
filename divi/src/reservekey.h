#ifndef RESERVE_KEY_H
#define RESERVE_KEY_H
#include <stdint.h>
#include <pubkey.h>
#include <I_KeypoolReserver.h>

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