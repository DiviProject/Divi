// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include <pubkey.h>
#include <netbase.h>
#include <sync.h>
#include <primitives/transaction.h>
#include <masternode-tier.h>
#include <MasternodePing.h>

#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MASTERNODE_PING_SECONDS (5 * 60)
#define MASTERNODE_EXPIRATION_SECONDS (120 * 60)
#define MASTERNODE_REMOVAL_SECONDS (130 * 60)
#define MASTERNODE_CHECK_SECONDS 5

class CMasternode;
class CMasternodeBroadcast;
class CMasternodePing;
class CMasternodeMan;
class CDataStream;

//
// The Masternode Class. For managing the Obfuscation process. It contains the input of the 10000 PIV, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode
{
protected:

    /** Cached block hash of where the collateral output of this
     *  masternode got included.  */
    mutable uint256 collateralBlock;
public:
    int64_t lastTimeChecked;
    enum state {
        MASTERNODE_ENABLED,
        MASTERNODE_EXPIRED,
        MASTERNODE_REMOVE,
        MASTERNODE_VIN_SPENT,
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    std::vector<unsigned char> signature;
    int activeState;
    int64_t sigTime; //mnb message time
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    MasternodeTier nTier;
    CMasternodePing lastPing;

    int64_t DeterministicTimeOffset() const;

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);

    void swap(CMasternode& first, CMasternode& second); // nothrow

    CMasternode& operator=(CMasternode from);

    friend bool operator==(const CMasternode& a, const CMasternode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMasternode& a, const CMasternode& b)
    {
        return !(a.vin == b.vin);
    }

    /** Calculates the score of the current masternode, based on the given
     *  scoring hash.  It should be the result of GetBlockHashForScoring of
     *  the target block height.  */
    uint256 CalculateScore(const uint256& scoringBlockHash) const;

    bool IsEnabled() const;

    static CAmount GetTierCollateralAmount(MasternodeTier tier);
    static MasternodeTier GetTierByCollateralAmount(CAmount nCollateral);
    static bool IsTierValid(MasternodeTier tier);
    static std::string TierToString(MasternodeTier tier);

    std::string Status() const;

    CScript GetPaymentScript() const;
    bool IsValidNetAddr() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(signature);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(collateralBlock);
        READWRITE(allowFreeTx);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);

        int tier;
        if (!ser_action.ForRead ())
            tier = static_cast<int> (nTier);
        READWRITE(tier);
        if (ser_action.ForRead ())
            nTier = static_cast<MasternodeTier> (tier);
    }
};


//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:
    CMasternodeBroadcast() = default;
    CMasternodeBroadcast(const CMasternode& mn);

    void Relay() const;
    std::string getMessageToSign() const;
    uint256 GetHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(signature);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);

        int tier;
        if (!ser_action.ForRead ())
            tier = static_cast<int> (nTier);
        READWRITE(tier);
        if (ser_action.ForRead ()) {
            nTier = static_cast<MasternodeTier> (tier);
            collateralBlock.SetNull();
        }
    }
};
#endif
