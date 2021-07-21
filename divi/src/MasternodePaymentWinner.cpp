#include <MasternodePaymentWinner.h>

#include <hash.h>
#include <Logging.h>
#include <protocol.h>
#include <net.h>
#include <MasternodeHelpers.h>
#include <boost/lexical_cast.hpp>

CMasternodePaymentWinner::CMasternodePaymentWinner()
    : vinMasternode()
    , scoringBlockHash()
    , nBlockHeight(0)
    , payee()
    , signature()
{
    scoringBlockHash.SetNull();
}
CMasternodePaymentWinner::CMasternodePaymentWinner(const CTxIn& vinIn, const int height, const uint256& hash)
    : vinMasternode(vinIn)
    , scoringBlockHash(hash)
    , nBlockHeight(height)
{

}


bool CMasternodePaymentWinner::ComputeScoreHash()
{
    if (GetBlockHashForScoring(scoringBlockHash, nBlockHeight))
        return true;

    LogPrint("masternode", "Failed to get scoring hash for winner of height %d\n", nBlockHeight);
    scoringBlockHash.SetNull();
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
    assert(!scoringBlockHash.IsNull());
    return scoringBlockHash;
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