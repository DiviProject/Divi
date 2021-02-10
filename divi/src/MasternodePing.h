#ifndef MASTERNODE_PING_H
#define MASTERNODE_PING_H
#include <vector>
#include <uint256.h>
#include <primitives/transaction.h>
#include <string>
#include <serialize.h>
//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//
class CMasternodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> signature;
    //removed stop

    CMasternodePing();
    std::string getMessageToSign() const;
    void Relay() const;

    uint256 GetHash() const;
    void swap(CMasternodePing& first, CMasternodePing& second);
    CMasternodePing& operator=(CMasternodePing from);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(signature);
    }

    friend bool operator==(const CMasternodePing& a, const CMasternodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CMasternodePing& a, const CMasternodePing& b)
    {
        return !(a == b);
    }
};
#endif // MASTERNODE_PING_H