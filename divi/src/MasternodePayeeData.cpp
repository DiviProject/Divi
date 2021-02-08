#include <MasternodePayeeData.h>
#include <hash.h>
#include <boost/lexical_cast.hpp>
#include <protocol.h>
#include <Logging.h>
#include <masternode.h>
#include <net.h>

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


CMasternodePaymentWinner::CMasternodePaymentWinner()
    : vinMasternode()
    , seedHash()
    , nBlockHeight(0)
    , payee()
    , signature()
{
    seedHash.SetNull();
}
CMasternodePaymentWinner::CMasternodePaymentWinner(const CTxIn& vinIn, const int height, const uint256& hash)
    : vinMasternode(vinIn)
    , seedHash(hash)
    , nBlockHeight(height)
{

}


bool CMasternodePaymentWinner::ComputeScoreHash()
{
    if (GetBlockHashForScoring(seedHash, nBlockHeight))
        return true;

    LogPrint("masternode", "Failed to get scoring hash for winner of height %d\n", nBlockHeight);
    seedHash.SetNull();
    return false;
}

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << payee;
    ss << nBlockHeight;
    ss << vinMasternode.prevout;

    return ss.GetHash();
}

void CMasternodePaymentWinner::AddPayee(const CScript& payeeIn)
{
    payee = payeeIn;
}

const uint256&
CMasternodePaymentWinner::GetScoreHash() const
{
    assert(!seedHash.IsNull());
    return seedHash;
}

std::string CMasternodePaymentWinner::ToString() const
{
    std::string ret = "";
    ret += vinMasternode.ToString();
    ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
    ret += ", " + payee.ToString();
    ret += ", " + boost::lexical_cast<std::string>((int)signature.size());
    return ret;
}

std::string CMasternodePaymentWinner::getMessageToSign() const
{
    return vinMasternode.prevout.ToStringShort() +
            boost::lexical_cast<std::string>(nBlockHeight) +
            payee.ToString();
}

void CMasternodePaymentWinner::Relay() const
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    RelayInv(inv);
}