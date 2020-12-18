#include <reservekey.h>
#include <keypool.h>

CReserveKey::CReserveKey(
    I_KeypoolReserver& keypoolReserver): keypoolReserver_(keypoolReserver)
{
    nIndex = -1;
    fInternal = false;
}

CReserveKey::~CReserveKey()
{
    ReturnKey();
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey, bool fInternalIn)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        keypoolReserver_.ReserveKeyFromKeyPool(nIndex, keypool, fInternalIn);
        if (nIndex != -1) {
            vchPubKey = keypool.vchPubKey;
        }
        else {
            return false;
        }
        fInternal = keypool.fInternal;
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1) {
        keypoolReserver_.KeepKey(nIndex);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1) {
        keypoolReserver_.ReturnKey(nIndex, fInternal);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}
