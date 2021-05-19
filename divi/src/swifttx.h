// Copyright (c) 2009-2012 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTTX_H
#define SWIFTTX_H

#include <primitives/transaction.h>
#include <string>

class CDataStream;
class CNode;
class CConsensusVote;
class CTransaction;
class CTransactionLock;

extern std::map<uint256, CTransaction> mapTxLockReq;
extern std::map<uint256, CTransaction> mapTxLockReqRejected;
extern std::map<COutPoint, uint256> mapLockedInputs;

#endif
