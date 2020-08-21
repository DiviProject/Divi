// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include <BlockRewards.h>
#include <key.h>
#include <masternode.h>
#include <boost/lexical_cast.hpp>
#include <LotteryCoinstakes.h>
#include <primitives/transaction.h>

class CBlock;
class CMasternodePayments;
class CMasternodePaymentWinner;
class CMasternodeBlockPayees;

extern CMasternodePayments masternodePayments;

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CBlockIndex *prevIndex);

LotteryCoinstakeData CalculateLotteryWinners(const CBlock& block, const CBlockIndex *prevBlockIndex, int nHeight);

class CMasternodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CMasternodePayee();
    CMasternodePayee(const CScript& payee, int nVotesIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from masternodes
class CMasternodeBlockPayees
{
private:
    mutable CCriticalSection cs_vecPayments;

public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayments;

    CMasternodeBlockPayees();
    CMasternodeBlockPayees(int nBlockHeightIn);

    CMasternodeBlockPayees(const CMasternodeBlockPayees& o)
      : nBlockHeight(o.nBlockHeight), vecPayments(o.vecPayments)
    {}
    CMasternodeBlockPayees(CMasternodeBlockPayees&& o)
      : nBlockHeight(o.nBlockHeight), vecPayments(std::move(o.vecPayments))
    {}

    void AddPayee(const CScript& payeeIn, int nIncrement);

    bool GetPayee(CScript& payee) const;
    bool HasPayeeWithVotes(const CScript& payee, int nVotesReq) const;

    bool IsTransactionValid(const CTransaction& txNew) const;
    std::string GetRequiredPaymentsString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CMasternodePaymentWinner
{
public:
    CTxIn vinMasternode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CMasternodePaymentWinner();
    CMasternodePaymentWinner(const CTxIn& vinIn);

    uint256 GetHash() const;

    bool Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode);
    bool IsValid(CNode* pnode, std::string& strError) const;
    bool SignatureValid() const;
    void Relay() const;

    void AddPayee(const CScript& payeeIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString() const;
};

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

    std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;
    std::map<uint256, int> mapMasternodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    mutable CCriticalSection cs_mapMasternodeBlocks;
    mutable CCriticalSection cs_mapMasternodePayeeVotes;

public:

    CMasternodePayments();

    void Clear();

    bool AddWinningMasternode(const CMasternodePaymentWinner &winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee) const;
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    bool IsScheduled(const CMasternode& mn, int nNotBlockHeight) const;

    bool CanVote(const COutPoint& outMasternode, int nBlockHeight);

    int GetMinMasternodePaymentsProto() const;
    void ProcessMessageMasternodePayments(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight) const;
    void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake) const;
    std::string ToString() const;

    /** Retrieves the payment winner for the given hash.  Returns null
     *  if there is no entry for that hash.  */
    const CMasternodePaymentWinner* GetPaymentWinnerForHash(const uint256& hash) const {
        return const_cast<CMasternodePayments*>(this)->GetPaymentWinnerForHash(hash);
    }
    CMasternodePaymentWinner* GetPaymentWinnerForHash(const uint256& hash) {
        const auto mit = mapMasternodePayeeVotes.find(hash);
        if (mit == mapMasternodePayeeVotes.end())
            return nullptr;
        return &mit->second;
    }

    /** Retrieves the payees for the given block.  Returns null if there is
     *  no matching entry.  */
    const CMasternodeBlockPayees* GetPayeesForHeight(const unsigned height) const {
        return const_cast<CMasternodePayments*>(this)->GetPayeesForHeight(height);
    }
    CMasternodeBlockPayees* GetPayeesForHeight(const unsigned height) {
        const auto mit = mapMasternodeBlocks.find(height);
        if (mit == mapMasternodeBlocks.end())
            return nullptr;
        return &mit->second;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapMasternodePayeeVotes);
        READWRITE(mapMasternodeBlocks);
    }
};


#endif
