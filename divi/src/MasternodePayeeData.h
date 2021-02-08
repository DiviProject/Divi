#ifndef MASTERNODE_PAYEE_DATA_H
#define MASTERNODE_PAYEE_DATA_H
#include <vector>
#include <set>
#include <string>
#include <script/script.h>
#include <serialize.h>
#include <sync.h>
#include <uint256.h>
#include <primitives/transaction.h>

class I_BlockSubsidyProvider;

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

    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayments;
    std::set<COutPoint> voters;

public:
    CMasternodeBlockPayees();
    explicit CMasternodeBlockPayees(int nBlockHeightIn);

    CMasternodeBlockPayees(const CMasternodeBlockPayees& o);
    CMasternodeBlockPayees(CMasternodeBlockPayees&& o);

    void CountVote(const COutPoint& voter, const CScript& payeeIn);

    bool CanVote(const COutPoint& voter) const;
    bool GetPayee(CScript& payee) const;
    bool HasPayeeWithVotes(const CScript& payee, int nVotesReq) const;

    bool IsTransactionValid(const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew) const;
    std::string GetRequiredPaymentsString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
        READWRITE(voters);
    }
};

// for storing the winning payments
class CMasternodePaymentWinner
{
public:
    CTxIn vinMasternode;

private:
    /* Masternode payment blocks are uniquely identified by the seed hash used
       for their scoring computation.  This seed hash is based on the block
       height, but one block height might have multiple hashes in case of a
       reorg (so the hash is the more robust identifier).  We use the block
       height for messages sent on the wire because that's what the protocol
       is, but translate them to the seed hash and use the seed has instead
       for internal storage and processing.  The block height is also used
       to check freshness, e.g. when only accepting payment messages for
       somewhat recent blocks.  */
    uint256 seedHash;
    int nBlockHeight;

public:
    const uint256& getSeedHash() const
    {
        return seedHash;
    }

    CScript payee;
    std::vector<unsigned char> signature;

    CMasternodePaymentWinner();

    explicit CMasternodePaymentWinner(const CTxIn& vinIn, const int height, const uint256& hash);

    uint256 GetHash() const;

    std::string getMessageToSign() const;
    void Relay() const;

    void AddPayee(const CScript& payeeIn);

    inline int GetHeight() const
    {
        return nBlockHeight;
    }

    const uint256& GetScoreHash() const;

    /** Computes the score hash from our height.  This has to be called
     *  explicitly after deserialising and before GetScoreHash() can
     *  be used.  Returns false if the hash could not be found.  */
    bool ComputeScoreHash();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(signature);

        if (nType == SER_DISK) {
            /* For saving in the on-disk cache files, we include the
               seed hash as well.  */
            READWRITE(seedHash);
        } else if (ser_action.ForRead()) {
            /* After parsing from network, the seedHash field is not set
               and must not be accessed (e.g. through GetScoreHash) until
               ComputeScoreHash() has been called explicitly in a place
               that is convenient.  We do this (rather than computing here
               right away) to prevent potential DoS vectors where we might
               want to perform some more validation before doing the
               expensive computation.  */
            seedHash.SetNull();
        }
    }

    std::string ToString() const;
};

#endif // MASTERNODE_PAYEE_DATA_H