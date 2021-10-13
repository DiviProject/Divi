#include <Account.h>
CAccount::CAccount(): vchPubKey()
{
    SetNull();
}

void CAccount::SetNull()
{
    vchPubKey = CPubKey();
}