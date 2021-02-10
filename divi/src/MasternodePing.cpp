#include <MasternodePing.h>

#include <hash.h>
#include <protocol.h>
#include <net.h>
#include <boost/lexical_cast.hpp>

CMasternodePing::CMasternodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    signature = std::vector<unsigned char>();
}

std::string CMasternodePing::getMessageToSign() const
{
    return vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
}

void CMasternodePing::Relay() const
{
    CInv inv(MSG_MASTERNODE_PING, GetHash());
    RelayInv(inv);
}

uint256 CMasternodePing::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    return ss.GetHash();
}
void CMasternodePing::swap(CMasternodePing& first, CMasternodePing& second) // nothrow
{
    // enable ADL (not necessary in our case, but good practice)
    // by swapping the members of two classes,
    // the two classes are effectively swapped
    std::swap(first.vin, second.vin);
    std::swap(first.blockHash, second.blockHash);
    std::swap(first.sigTime, second.sigTime);
    std::swap(first.signature, second.signature);
}
CMasternodePing& CMasternodePing::operator=(CMasternodePing from)
{
    swap(*this, from);
    return *this;
}