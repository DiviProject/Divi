// Copyright (c) 2020 The DIVI Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKSIGNING_H
#define BITCOIN_BLOCKSIGNING_H

class CBlock;
class CKeyStore;

/** Signs a block with the script used in the coinbase output (the miner/staker)
 *  as required for valid PoS blocks.  */
bool SignBlock (const CKeyStore& keystore, CBlock& block);

/** Checks the signature on a block and returns true if it is valid for
 *  a PoS block.  */
bool CheckBlockSignature (const CBlock& block);

#endif // BITCOIN_BLOCKSIGNING_H
