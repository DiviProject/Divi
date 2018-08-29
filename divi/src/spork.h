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
static const int SPORK_13_BLOCK_PAYMENTS                            = 10012;
static const int SPORK_14_TX_FEE                                    = 10013;
static const int SPORK_15_BLOCK_VALUE                               = 10014;
static const int SPORK_16_LOTTERY_TICKET_MIN_VALUE                  = 10015;

static const int SPORK_START                                            = SPORK_2_SWIFTTX_ENABLED;
static const int SPORK_END                                              = SPORK_16_LOTTERY_TICKET_MIN_VALUE;

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

template <class T>
using MultiValueSporkList = std::vector<std::pair<T, int64_t>>;

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

struct SporkMultiValue {
    SporkMultiValue(int nActivationBlockHeightIn = 0);

    virtual bool IsValid() const;
    virtual std::string ToString() const = 0;

    const int nActivationBlockHeight;
};

struct BlockPaymentSporkValue : public SporkMultiValue {
    BlockPaymentSporkValue();
    BlockPaymentSporkValue(int nStakeRewardIn, int nMasternodeRewardIn,
                           int nTreasuryRewardIn, int nProposalsRewardIn, int nCharityRewardIn, int nActivationBlockHeightIn);

    static BlockPaymentSporkValue FromString(std::string strData);

    bool IsValid() const override;
    std::string ToString() const override;

    const int nStakeReward;
    const int nMasternodeReward;
    const int nTreasuryReward;
    const int nProposalsReward;
    const int nCharityReward;
};

struct BlockSubsiditySporkValue : public SporkMultiValue {
    BlockSubsiditySporkValue();
    BlockSubsiditySporkValue(int nBlockSubsidityIn, int nActivationBlockHeightIn);

    static BlockSubsiditySporkValue FromString(std::string strData);

    bool IsValid() const override;
    std::string ToString() const override;

    const int nBlockSubsidity;
};

struct LotteryTicketMinValueSporkValue : public SporkMultiValue {
    LotteryTicketMinValueSporkValue();
    LotteryTicketMinValueSporkValue(int nEntryTicketValueIn, int nActivationBlockHeight);

    static LotteryTicketMinValueSporkValue FromString(std::string strData);

    bool IsValid() const override;
    std::string ToString() const override;

    const int nEntryTicketValue;
};

struct TxFeeSporkValue : public SporkMultiValue {
    TxFeeSporkValue();
    TxFeeSporkValue(int nTxValueMultiplierIn, int nTxSizeMultiplierIn, int nMaxFeeIn,
                    int nMinFeeIn, int nActivationBlockHeightIn);

    static TxFeeSporkValue FromString(std::string strData);

    bool IsValid() const override;
    std::string ToString() const override;

    const int nTxValueMultiplier;
    const int nTxSizeMultiplier;
    const int nMaxFee;
    const int nMinFee;
};

class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    // Some sporks require to have history, we will use sorted vector for this approach.
    std::map<int, std::vector<CSporkMessage>> mapSporksActive;

    CPubKey sporkPubKey;
    CKey sporkPrivKey;

private:
    void AddActiveSpork(const CSporkMessage &spork);
    bool IsNewerSpork(const CSporkMessage &spork) const;

public:

    CSporkManager();

    void LoadSporksFromDB();
    void ProcessSpork(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    void ExecuteSpork(int nSporkID, std::string strValue);
    bool UpdateSpork(int nSporkID, std::string strValue);
    int GetActiveSporkCount() const;

    bool IsSporkActive(int nSporkID);
    std::vector<CSporkMessage> GetMultiValueSpork(int nSporkID) const;

    template <class T>
    static void ConvertMultiValueSporkVector(const std::vector<CSporkMessage> &vMultiValueSprok, MultiValueSporkList<T> &vResult)
    {
        MultiValueSporkList<T> result;
        for(auto &&spork : vMultiValueSprok) {
            result.emplace_back(T::FromString(spork.strValue), spork.nTimeSigned);
        }

        vResult.swap(result);
    }

    template <class T>
    static T GetActiveMultiValueSpork(const MultiValueSporkList<T> &vSporks, int nHeight, int nBlockTime)
    {
        int nIndex = -1;
        for(size_t i = 0; i < vSporks.size(); ++i) {
            auto sporkEntry = vSporks.at(i);
            if(nHeight >= sporkEntry.first.nActivationBlockHeight && sporkEntry.second < nBlockTime) {
                nIndex = i;
            }
        }

        return nIndex >= 0 ? vSporks.at(nIndex).first : T();
    }

    std::string GetSporkValue(int nSporkID) const;
    int GetSporkIDByName(const std::string& strName);
    std::string GetSporkNameByID(int nSporkID);

    bool SetSporkAddress(const std::string& strPubKey);
    bool SetPrivKey(const std::string& strPrivKey);
};

#endif
