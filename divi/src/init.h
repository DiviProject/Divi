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
class I_ChainExtensionService;

namespace boost
{
class thread_group;
} // namespace boost

void StartShutdown();
bool ShutdownRequested();
void Shutdown();

void EnableMainSignals();
void EnableUnitTestSignals();

void InitializeMainBlockchainModules();
void FinalizeMainBlockchainModules();
bool InitializeDivi(boost::thread_group& threadGroup);

/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

void InitializeMultiWalletModule();
void FinalizeMultiWalletModule();

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
bool HasRecentlyAttemptedToGenerateProofOfStake();
#endif // BITCOIN_INIT_H
