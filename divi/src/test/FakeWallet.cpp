// Copyright (c) 2021 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/FakeWallet.h"

#include <blockmap.h>
#include "chain.h"
#include <chainparams.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
#include "merkletx.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include <Settings.h>
#include "sync.h"
#include "WalletTx.h"

#include <sstream>
#include <string>

extern Settings& settings;
namespace
{

LegacyWalletDatabaseEndpointFactory* getWalletDBEndpointFactory(const std::string walletFilename)
{
  return new LegacyWalletDatabaseEndpointFactory(walletFilename, settings);
}

/** Returns a unique wallet filename.  */
std::string GetWalletFilename()
{
  static int cnt = 0;
  std::ostringstream res;
  res << "currentWallet" << (++cnt) << ".dat";
  return res.str();
}

/** Returns a HD seed for testing.  */
const CHDChain& getHDWalletSeedForTesting()
{
  static CCriticalSection cs_testing;
  static bool toBeConstructed = true;
  static CHDChain hdchain;
  LOCK(cs_testing);

  if (toBeConstructed)
  {
    CHDChain::setNewHDChain(hdchain);
    toBeConstructed = false;
  }

  return hdchain;
}

/** Creates a "normal" transaction paying to the given script.  */
CMutableTransaction createDefaultTransaction(const CScript& defaultScript, unsigned& index,
                                             const CAmount amount)
{
  static int nextLockTime = 0;
  const int numberOfIndices = GetRand(10) + 1;
  index = GetRand(numberOfIndices);

  CMutableTransaction tx;
  tx.nLockTime = nextLockTime++; // so all transactions get different hashes
  tx.vout.resize(numberOfIndices);
  for(CTxOut& output: tx.vout)
  {
    output.nValue = 1;
    output.scriptPubKey = CScript() << OP_TRUE;
  }
  tx.vout[index].nValue = amount;
  tx.vout[index].scriptPubKey = defaultScript;
  tx.vin.resize(1);
  // Avoid flagging as a coinbase tx
  tx.vin[0].prevout = COutPoint(GetRandHash(),static_cast<uint32_t>(GetRand(100)) );

  return tx;
}

} // anonymous namespace

FakeWallet::FakeWallet(FakeBlockIndexWithHashes& c)
  : walletFilename_(GetWalletFilename())
  , fakeChain(c)
  , databaseEndpointFactory_(getWalletDBEndpointFactory(walletFilename_))
  , confirmationsCalculator_(new FakeMerkleTxConfirmationNumberCalculator(*fakeChain.activeChain, *fakeChain.blockIndexByHash))
  , wrappedWallet_()
{
  {
    wrappedWallet_.reset(new CWallet(walletFilename_,*databaseEndpointFactory_,*fakeChain.activeChain, *fakeChain.blockIndexByHash, *confirmationsCalculator_));
    wrappedWallet_->LoadWallet();
    wrappedWallet_->GetDatabaseBackend()->WriteHDChain(getHDWalletSeedForTesting());
    wrappedWallet_.reset();
  }
  wrappedWallet_.reset(new CWallet(walletFilename_,*databaseEndpointFactory_, *fakeChain.activeChain, *fakeChain.blockIndexByHash, *confirmationsCalculator_));
  wrappedWallet_->SetDefaultKeyTopUp(3);
  wrappedWallet_->LoadWallet();
}

FakeWallet::FakeWallet(FakeBlockIndexWithHashes& c, std::string walletFilename)
  : walletFilename_(walletFilename)
  , fakeChain(c)
  , databaseEndpointFactory_(getWalletDBEndpointFactory(walletFilename_))
  , confirmationsCalculator_(new FakeMerkleTxConfirmationNumberCalculator(*fakeChain.activeChain, *fakeChain.blockIndexByHash))
  , wrappedWallet_(new CWallet(walletFilename_, *databaseEndpointFactory_, *fakeChain.activeChain, *fakeChain.blockIndexByHash, *confirmationsCalculator_))
{
  wrappedWallet_->LoadWallet();
}

FakeWallet::~FakeWallet()
{
  wrappedWallet_.reset();
  confirmationsCalculator_.reset();
  databaseEndpointFactory_.reset();
}

void FakeWallet::AddBlock()
{
  fakeChain.addBlocks(1, version);
}

void FakeWallet::AddConfirmations(const unsigned numConf, const int64_t minAge)
{
  fakeChain.addBlocks(numConf, version, fakeChain.activeChain->Tip()->nTime + minAge);
}

const CWalletTx& FakeWallet::AddDefaultTx(const CScript& scriptToPayTo, unsigned& outputIndex,
                                          const CAmount amount)
{
  const CTransaction tx = createDefaultTransaction(scriptToPayTo, outputIndex, amount);
  CWalletTx wtx(tx);
  wrappedWallet_->AddToWallet(wtx);
  const CWalletTx* txPtr = wrappedWallet_->GetWalletTx(wtx.GetHash());
  assert(txPtr);
  return *txPtr;
}

void FakeWallet::FakeAddToChain(const CWalletTx& tx)
{
  auto* txPtr = const_cast<CWalletTx*>(&tx);
  txPtr->hashBlock = fakeChain.activeChain->Tip()->GetBlockHash();
  txPtr->merkleBranchIndex = 0;
  txPtr->fMerkleVerified = true;
  wrappedWallet_->GetDatabaseBackend()->WriteTx(txPtr->GetHash(),*txPtr);
}

bool FakeWallet::TransactionIsInMainChain(const CWalletTx* walletTx) const
{
  const BlockMap& blockIndexByHash = *(fakeChain.blockIndexByHash);
  BlockMap::const_iterator it = blockIndexByHash.find(walletTx->hashBlock);
  if(it != blockIndexByHash.end())
  {
    return fakeChain.activeChain->Contains(it->second);
  }
  else
  {
    return false;
  }
}

void FakeWallet::SetConfirmedTxsToVerified()
{
  std::vector<const CWalletTx*> allTransactions = wrappedWallet_->GetWalletTransactionReferences();
  for(auto txPtrCopy: allTransactions)
  {
    if(TransactionIsInMainChain(txPtrCopy))
    {
      const_cast<CWalletTx*>(txPtrCopy)->fMerkleVerified = true;
    }
  }
}

CPubKey FakeWallet::getNewKey()
{
  CPubKey nextKeyGenerated;
  wrappedWallet_->GetKeyFromPool(nextKeyGenerated, false);
  return nextKeyGenerated;
}
