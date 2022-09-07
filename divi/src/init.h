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
class CTxMemPool;
class I_MerkleTxConfirmationNumberCalculator;
class CChainParams;
class CChain;
class BlockMap;
class I_BlockSubmitter;

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

/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

void InitializeMultiWalletModule();
void FinalizeMultiWalletModule();

class MasternodeModule;
class I_ChainExtensionService;
void InitializeChainExtensionModule(const MasternodeModule& masternodeModule);
void FinalizeChainExtensionModule();
const I_ChainExtensionService& GetChainExtensionService();
const I_BlockSubmitter& GetBlockSubmitter();

bool VerifyChain(int nCheckLevel, int nCheckDepth, bool useCoinTip);
CTxMemPool& GetTransactionMemoryPool();
bool ManualBackupWallet(const std::string& strDest);
CWallet* GetWallet();
const I_MerkleTxConfirmationNumberCalculator& GetConfirmationsCalculator();
bool LoadAndSelectWallet(const std::string& walletFilename, bool initializeBackendSettings);
void RestartCoinMintingModuleWithReloadedWallet();
bool ConnectGenesisBlock();
#endif // BITCOIN_INIT_H
