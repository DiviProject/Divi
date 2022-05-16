// Copyright (c) 2021 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FAKE_WALLET_H
#define FAKE_WALLET_H

#include "amount.h"
#include "wallet.h"

#include "test/FakeBlockIndexChain.h"
#include "test/FakeMerkleTxConfirmationNumberCalculator.h"

#include <cstdint>

class CScript;

/** A wallet with (mostly) real seed and keys, which can be used in tests
 *  that need to exercise wallet functions.
 *
 *  The FakeWallet will hold cs_wallet automatically.  */
class FakeWallet
{

private:

  /** Version for blocks to add.  */
  static constexpr int32_t version = 1;

  /** The fake chain that we use for the wallet.  */
  FakeBlockIndexWithHashes& fakeChain;
  std::unique_ptr<FakeMerkleTxConfirmationNumberCalculator> confirmationsCalculator_;
  mutable std::unique_ptr<CWallet> wrappedWallet_;

public:

  /** Constructs the wallet with a given external fake chain.  */
  explicit FakeWallet(FakeBlockIndexWithHashes& c);
  explicit FakeWallet(FakeBlockIndexWithHashes& c, std::string walletFilename);
  ~FakeWallet ();

  CWallet& getWallet() { return *wrappedWallet_; }
  const CWallet& getWallet() const { return *wrappedWallet_; }

  /** Adds a new block to our fake chain.  */
  void AddBlock();

  /** Adds a couple of new blocks and bumps the time at least
   *  the given amount.  This can be used to make sure some
   *  coins fake added to the chain have at least a given age
   *  and number of confirmations.  */
  void AddConfirmations(unsigned numConf, int64_t minAge = 0);

  /** Adds a new ordinary transaction to the wallet, paying a given amount
   *  to a given script.  The transaction is returned, and the output index
   *  with the output to the requested script is set.  */
  const CWalletTx& AddDefaultTx(const CScript& scriptToPayTo, unsigned& outputIndex,
                                CAmount amount = 100 * COIN);

  bool TransactionIsInMainChain(const CWalletTx* walletTx) const;
  void SetConfirmedTxsToVerified();

  /** Modifies the given wallet transaction to look like it was included in the
   *  current top block.  */
  void FakeAddToChain(const CWalletTx& tx);

  /** Returns a new key from the wallet.  */
  CPubKey getNewKey();

};

#endif // FAKE_WALLET_H
