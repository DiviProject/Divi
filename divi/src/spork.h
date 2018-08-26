// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#include "obfuscation.h"
#include "protocol.h"
#include <boost/lexical_cast.hpp>

class CSporkMessage;
class CSporkManager;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
static const int SPORK_2_SWIFTTX_ENABLED                            = 10001;
static const int SPORK_3_SWIFTTX_BLOCK_FILTERING                    = 10002;
static const int SPORK_5_INSTANTSEND_MAX_VALUE                      = 10004;
static const int SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT             = 10007;
static const int SPORK_9_SUPERBLOCKS_ENABLED                        = 10008;
static const int SPORK_10_MASTERNODE_PAY_UPDATED_NODES              = 10009;
static const int SPORK_12_RECONSIDER_BLOCKS                         = 10011;

static const int SPORK_START                                            = SPORK_2_SWIFTTX_ENABLED;
static const int SPORK_END                                              = SPORK_12_RECONSIDER_BLOCKS;

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    std::string strValue;
    int64_t nTimeSigned;

    CSporkMessage(int nSporkID, std::string strValueIn, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        strValue(strValueIn),
        nTimeSigned(nTimeSigned)
        {}

    CSporkMessage() :
        nSporkID(0),
        strValue(),
        nTimeSigned(0)
        {}


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nSporkID);
        READWRITE(strValue);
        READWRITE(nTimeSigned);
        if (!(nType & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool Sign(const CKey& key, const CPubKey &sporkPubKey);
    bool CheckSignature(const CPubKey &pubKey) const;
    void Relay();
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    std::map<int, CSporkMessage> mapSporksActive;

    CPubKey sporkPubKey;
    CKey sporkPrivKey;

public:

    CSporkManager() {}

    void LoadSporksFromDB();
    void ProcessSpork(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    void ExecuteSpork(int nSporkID, std::string strValue);
    bool UpdateSpork(int nSporkID, std::string strValue);

    bool IsSporkActive(int nSporkID);
    std::string GetSporkValue(int nSporkID) const;
    int GetSporkIDByName(const std::string& strName);
    std::string GetSporkNameByID(int nSporkID);

    bool SetSporkAddress(const std::string& strPubKey);
    bool SetPrivKey(const std::string& strPrivKey);
};

#endif
