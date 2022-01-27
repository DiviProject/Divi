// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include <string>
#include <boost/signals2/signal.hpp>
#include <set>
class CWallet;
class CBlockIndex;

namespace boost
{
class thread_group;
} // namespace boost

void StartShutdown();
bool ShutdownRequested();
void Shutdown();

void EnableMainSignals();
void EnableUnitTestSignals();
bool InitializeDivi(boost::thread_group& threadGroup);
int ScanForWalletTransactions(CWallet& walletToRescan, const CBlockIndex* scanStartIndex = nullptr);
void InitializeWallet(std::string strWalletFile);
void DeallocateWallet();
#endif // BITCOIN_INIT_H
