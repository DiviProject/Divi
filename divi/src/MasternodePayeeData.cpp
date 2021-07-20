#include <MasternodePayeeData.h>
#include <hash.h>
#include <boost/lexical_cast.hpp>
#include <protocol.h>
#include <Logging.h>
#include <masternode.h>
#include <net.h>
#include <MasternodeHelpers.h>
#include <script/standard.h>
#include <utilmoneystr.h>
#include <I_BlockSubsidyProvider.h>

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

const int CMasternodeBlockPayees::MNPAYMENTS_SIGNATURES_REQUIRED = 6;

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

bool CMasternodeBlockPayees::IsTransactionValid(const I_BlockSubsidyProvider& subsidies, const CTransaction& txNew) const
{
    LOCK(cs_vecPayments);
    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";
    auto rewards = subsidies.GetBlockSubsidity(nBlockHeight);

    CAmount requiredMasternodePayment = rewards.nMasternodeReward;

    //require at least 6 signatures
    for(const auto& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= CMasternodeBlockPayees::MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < CMasternodeBlockPayees::MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (const auto& payee : vecPayments) {
        bool found = false;
        for (const auto& out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredMasternodePayment)
                    found = true;
                else
                    LogPrint("masternode","Masternode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue), FormatMoney(requiredMasternodePayment));
            }
        }

        if (payee.nVotes >= CMasternodeBlockPayees::MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            std::string addressString = ExtractDestination(payee.scriptPubKey);
            if (strPayeesPossible == "") {
                strPayeesPossible += addressString;
            } else {
                strPayeesPossible += "," + addressString;
            }
        }
    }

    LogPrint("masternode","CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment), strPayeesPossible);
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString() const
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (const auto& payee : vecPayments) {
        std::string addressString = ExtractDestination(payee.scriptPubKey);
        if (ret != "Unknown") {
            ret += ", " + addressString + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = addressString + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
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