// Copyright (c) 2021 The Divi developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FAKE_WALLET_H
#define FAKE_WALLET_H

#include "amount.h"
#include "wallet.h"

#include "test/FakeBlockIndexChain.h"

#include <cstdint>

class CScript;

/** A wallet with (mostly) real seed and keys, which can be used in tests
 *  that need to exercise wallet functions.  */
class FakeWallet : public CWallet
{

private:
  
  /** Version for blocks to add.  */
  static constexpr int32_t version = 1;

  /** The fake chain that we use for the wallet.  */
  FakeBlockIndexWithHashes& fakeChain;

public:

  /** Constructs the wallet with a given external fake chain.  */
  explicit FakeWallet(FakeBlockIndexWithHashes& c);

  /** Adds a new block to our fake chain.  */
  void AddBlock();

  /** Adds a new ordinary transaction to the wallet, paying a given amount
   *  to a given script.  The transaction is returned, and the output index
   *  with the output to the requested script is set.  */
  const CWalletTx& AddDefaultTx(const CScript& scriptToPayTo, unsigned& outputIndex,
                                CAmount amount = 100 * COIN);

  /** Modifies the given wallet transaction to look like it was included in the
   *  current top block.  */
  void FakeAddToChain(const CWalletTx& tx);

  /** Returns a new key from the wallet.  */
  CPubKey getNewKey();

};

#endif // FAKE_WALLET_H
