// Copyright (c) 2021 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/FakeWallet.h"

#include "chain.h"
#include "merkletx.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include "sync.h"
#include "WalletTx.h"

#include <sstream>
#include <string>

namespace
{

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
    FakeBlockIndexWithHashes dummyChain(1, 1600000000, 1);
    CWallet wallet("test_wallet.dat", *dummyChain.activeChain, *dummyChain.blockIndexByHash);
    wallet.defaultKeyPoolTopUp = 1;
    bool firstLoad = true;
    wallet.LoadWallet(firstLoad);
    wallet.GenerateNewHDChain();
    wallet.GetHDChain(hdchain);
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
  : CWallet(GetWalletFilename(), *c.activeChain, *c.blockIndexByHash),
    fakeChain(c)
{
  defaultKeyPoolTopUp = (int64_t)3;
  bool firstLoad = true;
  CPubKey newDefaultKey;
  LoadWallet(firstLoad);
  //GenerateNewHDChain();
  SetHDChain(getHDWalletSeedForTesting(),false);
  SetMinVersion(FEATURE_HD);
  GetKeyFromPool(newDefaultKey, false);
  SetDefaultKey(newDefaultKey);

  ENTER_CRITICAL_SECTION(cs_wallet);
}

FakeWallet::~FakeWallet()
{
  LEAVE_CRITICAL_SECTION(cs_wallet);
}

void FakeWallet::AddBlock()
{
  fakeChain.addBlocks(1, version);
}

const CWalletTx& FakeWallet::AddDefaultTx(const CScript& scriptToPayTo, unsigned& outputIndex,
                                          const CAmount amount)
{
  const CMutableTransaction tx = createDefaultTransaction(scriptToPayTo, outputIndex, amount);
  const CMerkleTx merkleTx(tx, *fakeChain.activeChain, *fakeChain.blockIndexByHash);
  const CWalletTx wtx(merkleTx);
  AddToWallet(wtx);
  const CWalletTx* txPtr = GetWalletTx(wtx.GetHash());
  assert(txPtr);
  return *txPtr;
}

void FakeWallet::FakeAddToChain(const CWalletTx& tx)
{
  auto* txPtr = const_cast<CWalletTx*>(&tx);
  txPtr->hashBlock = fakeChain.activeChain->Tip()->GetBlockHash();
  txPtr->nIndex = 0;
  txPtr->fMerkleVerified = true;
}

CPubKey FakeWallet::getNewKey()
{
  CPubKey nextKeyGenerated;
  GetKeyFromPool(nextKeyGenerated, false);
  return nextKeyGenerated;
}
