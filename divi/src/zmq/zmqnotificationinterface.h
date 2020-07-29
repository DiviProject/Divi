// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
#define BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H

#include "NotificationInterface.h"
#include <string>
#include <map>

class CBlockIndex;
class CZMQAbstractNotifier;
class Settings;

class CZMQNotificationInterface : public NotificationInterface
{
public:
    virtual ~CZMQNotificationInterface();

    static CZMQNotificationInterface* CreateWithArguments(const Settings &settings);

protected:
    bool Initialize();
    void Shutdown();

    // NotificationInterface
    void SyncTransaction(const CTransaction &tx, const CBlock *pblock) override;
    void UpdatedBlockTip(const CBlockIndex *pindex) override;
    void NotifyTransactionLock(const CTransaction &tx) override;

private:
    CZMQNotificationInterface();

    void *pcontext;
    std::list<CZMQAbstractNotifier*> notifiers;
};

#endif // BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
