#ifndef WALLET_TX_H
#define WALLET_TX_H

#include <merkletx.h>
#include <utilstrencodings.h>
#include <tinyformat.h>
#include <amount.h>
#include <vector>
#include <utility>
#include <serialize.h>
#include <string>
#include <map>
#include <stdint.h>

typedef std::map<std::string, std::string> mapValue_t;
void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue);
void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue);

/**
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx final: public CMerkleTx
{
public:
    std::map<std::string, std::string> mapValue;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived; //! time received by this node
    unsigned int nTimeSmart;
    char createdByMe;
    std::string strFromAccount;
    int64_t nOrderPos; //! position in ordered transaction list

    CWalletTx();
    CWalletTx(const CTransaction& txIn);
    void Init();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        if (ser_action.ForRead())
            Init();
        char fSpent = false;

        if (!ser_action.ForRead()) {
            mapValue["fromaccount"] = strFromAccount;

            WriteOrderPos(nOrderPos, mapValue);

            if (nTimeSmart)
                mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        READWRITE(*(CMerkleTx*)this);
        if (!ser_action.ForRead())
        {
            uint64_t unused = 0;
            WriteCompactSize(s,unused);
        }
        else
        {
            ReadCompactSize(s);
        }
        READWRITE(mapValue);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(createdByMe);
        READWRITE(fSpent);

        if (ser_action.ForRead()) {
            strFromAccount = mapValue["fromaccount"];

            ReadOrderPos(nOrderPos, mapValue);

            nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(mapValue["timesmart"]) : 0;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("version");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    int64_t GetTxTime() const;
    bool UpdateTransaction(const CWalletTx& other, bool reorg);
};
#endif// WALLET_TX_H