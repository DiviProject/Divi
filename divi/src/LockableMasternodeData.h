#ifndef LOCKABLE_MASTERNODE_DATA_H
#define LOCKABLE_MASTERNODE_DATA_H
#include  <sync.h>
#include <masternode.h>
#include <vector>
struct LockableMasternodeData
{
public:
    CCriticalSection& cs;
    std::vector<CMasternode>& masternodes;
    LockableMasternodeData(
        CCriticalSection& csToLock,
        std::vector<CMasternode>& masternodeData
        ): cs(csToLock)
        , masternodes(masternodeData)
    {
    }
};

#endif// LOCKABLE_MASTERNODE_DATA_H