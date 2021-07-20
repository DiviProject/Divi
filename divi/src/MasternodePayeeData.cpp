#include <MasternodePayeeData.h>
#include <hash.h>
#include <boost/lexical_cast.hpp>
#include <protocol.h>
#include <Logging.h>
#include <masternode.h>
#include <net.h>
#include <MasternodeHelpers.h>

CMasternodePayee::CMasternodePayee()
{
    scriptPubKey = CScript();
    nVotes = 0;
}

CMasternodePayee::CMasternodePayee(const CScript& payee, int nVotesIn)
{
    scriptPubKey = payee;
    nVotes = nVotesIn;
}

CMasternodeBlockPayees::CMasternodeBlockPayees()
{
    nBlockHeight = 0;
    vecPayments.clear();
}

CMasternodeBlockPayees::CMasternodeBlockPayees(const CMasternodeBlockPayees& o)
    : nBlockHeight(o.nBlockHeight), vecPayments(o.vecPayments), voters(o.voters)
{
}
CMasternodeBlockPayees::CMasternodeBlockPayees(CMasternodeBlockPayees&& o)
    : nBlockHeight(o.nBlockHeight), vecPayments(std::move(o.vecPayments)), voters(std::move(o.voters))
{
}

CMasternodeBlockPayees::CMasternodeBlockPayees(int nBlockHeightIn)
{
    nBlockHeight = nBlockHeightIn;
    vecPayments.clear();
}

void CMasternodeBlockPayees::CountVote(const COutPoint& voter, const CScript& payeeIn)
{
    LOCK(cs_vecPayments);

    const auto ins = voters.insert(voter);
    assert(ins.second);

    for (auto& payee : vecPayments) {
        if (payee.scriptPubKey == payeeIn) {
            ++payee.nVotes;
            return;
        }
    }

    CMasternodePayee c(payeeIn, 1);
    vecPayments.push_back(c);
}

bool CMasternodeBlockPayees::CanVote(const COutPoint& voter) const
{
    LOCK(cs_vecPayments);
    return voters.count(voter) == 0;
}

bool CMasternodeBlockPayees::GetPayee(CScript& payee) const
{
    LOCK(cs_vecPayments);

    int nVotes = -1;
    for (const auto& p : vecPayments) {
        if (p.nVotes > nVotes) {
            payee = p.scriptPubKey;
            nVotes = p.nVotes;
        }
    }

    return (nVotes > -1);
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(const CScript& payee, int nVotesReq) const
{
    LOCK(cs_vecPayments);

    for (const auto& p : vecPayments) {
        if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
    }

    return false;
}

std::vector<CMasternodePayee> CMasternodeBlockPayees::GetPaymentVotes() const
{
    LOCK(cs_vecPayments);
    std::vector<CMasternodePayee> paymentVotes = vecPayments;
    return paymentVotes;
}
int CMasternodeBlockPayees::GetHeight() const
{
    return nBlockHeight;
}