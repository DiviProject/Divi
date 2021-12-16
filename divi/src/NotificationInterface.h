// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATIONINTERFACE_H
#define BITCOIN_VALIDATIONINTERFACE_H

#include <boost/signals2/signal.hpp>
#include <boost/shared_ptr.hpp>
#include <unordered_set>

class CBlock;
struct CBlockLocator;
class CBlockIndex;
class CReserveScript;
class CTransaction;
class NotificationInterface;
class CValidationState;
class uint256;

enum TransactionSyncType
{
    MEMPOOL_TX_ADD,
    CONFLICTED_TX,
    BLOCK_DISCONNECT,
    NEW_BLOCK,
    RESCAN,
};

struct MainNotificationSignals {
    /** Notifies listeners of updated block chain tip */
    boost::signals2::signal<void (const CBlockIndex *)> UpdatedBlockTip;
    /** Notifies listeners of updated transaction data (transaction, and optionally the block it is found in. */
    boost::signals2::signal<void (const CTransaction &, const CBlock *,const TransactionSyncType)> SyncTransaction;
    /** Notifies listeners of a new active block chain. */
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    /** Tells listeners to broadcast their data. */
    boost::signals2::signal<void ()> RebroadcastWalletTransactions;
};

// These functions dispatch to one or all registered wallets
class NotificationInterfaceRegistry
{
private:
    static MainNotificationSignals g_signals;
    std::unordered_set<NotificationInterface*> registeredInterfaces;
public:
    /** Register a wallet to receive updates from core */
    void RegisterValidationInterface(NotificationInterface* pwalletIn);
    /** Unregister a wallet from core */
    void UnregisterValidationInterface(NotificationInterface* pwalletIn);
    /** Unregister all wallets from core */
    void UnregisterAllValidationInterfaces();

    MainNotificationSignals& getSignals() const;
};

class NotificationInterface {
protected:
    virtual void UpdatedBlockTip(const CBlockIndex *pindex) {}
    virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock, const TransactionSyncType) {}
    virtual void SetBestChain(const CBlockLocator &locator) {}
    virtual void RebroadcastWalletTransactions() {}
public:
    /** (Un)Register a wallet to receive updates from core */
    void RegisterWith(MainNotificationSignals&);
    void UnregisterWith(MainNotificationSignals&);
};

#endif // BITCOIN_VALIDATIONINTERFACE_H
