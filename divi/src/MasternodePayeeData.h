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
#include <MasternodePaymentWinner.h>

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
    std::vector<CMasternodePayee> GetPaymentVotes() const;
    int GetHeight() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
        READWRITE(voters);
    }
};
#endif // MASTERNODE_PAYEE_DATA_H