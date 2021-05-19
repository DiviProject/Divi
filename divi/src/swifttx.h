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
extern std::map<uint256, CConsensusVote> mapTxLockVote;
extern std::map<uint256, CTransactionLock> mapTxLocks;
extern std::map<COutPoint, uint256> mapLockedInputs;

class CConsensusVote
{
public:
    CTxIn vinMasternode;
    uint256 txHash;
    int nBlockHeight;
    std::vector<unsigned char> vchMasterNodeSignature;

    uint256 GetHash() const;

    bool SignatureValid();
    bool Sign();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(txHash);
        READWRITE(vinMasternode);
        READWRITE(vchMasterNodeSignature);
        READWRITE(nBlockHeight);
    }
};

class CTransactionLock
{
public:
    int nBlockHeight;
    uint256 txHash;
    std::vector<CConsensusVote> vecConsensusVotes;
    int nExpiration;
    int nTimeout;

    bool SignaturesValid();
    int CountSignatures();
    void AddSignature(CConsensusVote& cv);

    uint256 GetHash() const
    {
        return txHash;
    }
};


#endif
