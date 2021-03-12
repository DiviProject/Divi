// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "key.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include <primitives/transaction.h>


#include "masternodeconfig.h"
#include "masternode-tier.h"

#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MASTERNODE_PING_SECONDS (5 * 60)
#define MASTERNODE_EXPIRATION_SECONDS (120 * 60)
#define MASTERNODE_REMOVAL_SECONDS (130 * 60)
#define MASTERNODE_CHECK_SECONDS 5

class CMasternode;
class CMasternodeBroadcast;
class CMasternodeBroadcastFactory;
class CMasternodePing;
class CMasternodeMan;

/** Returns the block hash that is used in the masternode scoring / ranking
 *  logic for the winner of block nBlockHeight.  (It is the hash of the
 *  block nBlockHeight-101, but that's an implementation detail.)  */
bool GetBlockHashForScoring(uint256& hash, int nBlockHeight);

/** Returns the scoring hash corresponding to the given CBlockIndex
 *  offset by N.  In other words, that is used to compute the winner
 *  that should be payed in block pindex->nHeight+N.
 *
 *  In contrast to GetBlockHashForScoring, this works entirely independent
 *  of chainActive, and is guaranteed to look into the correct ancestor
 *  chain independent of potential reorgs.  */
bool GetBlockHashForScoring(uint256& hash,
                            const CBlockIndex* pindex, const int offset);

//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//

class CMasternodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CMasternodePing();
    CMasternodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    /** Verifies if the ping is valid for the given masternode.
     *  If it is, the method returns true and updates the last
     *  ping stored with the masternode.  */
    bool CheckAndUpdate(CMasternode& mn, int& nDos, bool fRequireEnabled = true) const;
    std::string getMessageToSign() const;
    bool Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode, bool updateTimeBeforeSigning = true);
    void Relay() const;

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CMasternodePing& first, CMasternodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CMasternodePing& operator=(CMasternodePing from)
    {
        swap(*this, from);
        return *this;
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

//
// The Masternode Class. For managing the Obfuscation process. It contains the input of the 10000 PIV, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

protected:

    /** Cached block hash of where the collateral output of this
     *  masternode got included.  */
    mutable uint256 collateralBlock;
public:
    int64_t lastTimeChecked;
    enum state {
        MASTERNODE_PRE_ENABLED,
        MASTERNODE_ENABLED,
        MASTERNODE_EXPIRED,
        MASTERNODE_OUTPOINT_SPENT,
        MASTERNODE_REMOVE,
        MASTERNODE_WATCHDOG_EXPIRED,
        MASTERNODE_POSE_BAN,
        MASTERNODE_VIN_SPENT,
        MASTERNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    MasternodeTier nTier;
    CMasternodePing lastPing;

    static bool IsCoinSpent(const COutPoint &outpoint, const MasternodeTier mnTier);
    /** Looks up and returns the block index when the collateral got
     *  included in the currently active chain.  If it is not yet confirmed
     *  then this returns nullptr.  */
    const CBlockIndex* GetCollateralBlock() const;

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
     *  seed hash.  It should be the result of GetBlockHashForScoring of
     *  the target block height.  */
    uint256 CalculateScore(const uint256& seedHash) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(sig);
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

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds) const;

    bool TimeSinceLastPingIsWithin(int seconds, int64_t now = -1) const;
    bool IsTooEarlyToReceivePingUpdate(int64_t now) const;
    bool IsTooEarlyToSendPingUpdate(int64_t now) const;

    void Disable();

    bool IsEnabled() const;

    int GetMasternodeInputAge() const;

    static CAmount GetTierCollateralAmount(MasternodeTier tier);
    static MasternodeTier GetTierByCollateralAmount(CAmount nCollateral);
    static bool IsTierValid(MasternodeTier tier);
    static std::string TierToString(MasternodeTier tier);

    std::string GetStatus() const;

    std::string Status() const;

    int64_t GetLastPaid(unsigned numberOfBlocksToSearchBack) const;
    bool IsValidNetAddr() const;
};


//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:
    CMasternodeBroadcast() = default;
    CMasternodeBroadcast(
        CService newAddr,
        CTxIn newVin,
        CPubKey pubKeyCollateralAddress,
        CPubKey pubKeyMasternode,
        MasternodeTier nMasternodeTier,
        int protocolVersionIn);
    CMasternodeBroadcast(const CMasternode& mn);

    bool Sign(CKey& keyCollateralAddress, bool updateTimeBeforeSigning = true);
    void Relay() const;
    std::string getMessageToSign() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(sig);
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

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

};

class CMasternodeBroadcastFactory
{
public:
    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(const CMasternodeConfig::CMasternodeEntry configEntry,
                       std::string& strErrorRet,
                       CMasternodeBroadcast& mnbRet,
                       bool fOffline = false,
                       bool deferRelay = false);

    static bool Create(const CMasternodeConfig::CMasternodeEntry configEntry,
                       CPubKey pubkeyCollateralAddress,
                       std::string& strErrorRet,
                       CMasternodeBroadcast& mnbRet,
                       bool fOffline = false);
private:
    static void createWithoutSignatures(
        CTxIn txin,
        CService service,
        CPubKey pubKeyCollateralAddressNew,
        CPubKey pubKeyMasternodeNew,
        MasternodeTier nMasternodeTier,
        bool deferRelay,
        CMasternodeBroadcast& mnbRet);

    static bool signPing(
        CKey keyMasternodeNew,
        CPubKey pubKeyMasternodeNew,
        CMasternodePing& mnp,
        std::string& strErrorRet);

    static bool signBroadcast(
        CKey keyCollateralAddressNew,
        CMasternodeBroadcast& mnb,
        std::string& strErrorRet);

    static bool provideSignatures(
        CKey keyMasternodeNew,
        CPubKey pubKeyMasternodeNew,
        CKey keyCollateralAddressNew,
        CMasternodeBroadcast& mnb,
        std::string& strErrorRet);

    static bool Create(CTxIn vin,
                        CService service,
                        CKey keyCollateralAddressNew,
                        CPubKey pubKeyCollateralAddressNew,
                        CKey keyMasternodeNew,
                        CPubKey pubKeyMasternodeNew,
                        MasternodeTier nMasternodeTier,
                        std::string& strErrorRet,
                        CMasternodeBroadcast& mnbRet,
                        bool deferRelay);
    static bool checkBlockchainSync(std::string& strErrorRet, bool fOffline);
    static bool setMasternodeKeys(
        const std::string& strKeyMasternode,
        std::pair<CKey,CPubKey>& masternodeKeyPair,
        std::string& strErrorRet);
    static bool setMasternodeCollateralKeys(
        const std::string& txHash,
        const std::string& outputIndex,
        const std::string& service,
        bool collateralPrivKeyIsRemote,
        CTxIn& txin,
        std::pair<CKey,CPubKey>& masternodeCollateralKeyPair,
        std::string& error);
    static bool checkMasternodeCollateral(
        const CTxIn& txin,
        const std::string& txHash,
        const std::string& outputIndex,
        const std::string& service,
        MasternodeTier& nMasternodeTier,
        std::string& strErrorRet);
    static bool createArgumentsFromConfig(
        const CMasternodeConfig::CMasternodeEntry configEntry,
        std::string& strErrorRet,
        bool fOffline,
        bool collateralPrivKeyIsRemote,
        CTxIn& txin,
        std::pair<CKey,CPubKey>& masternodeKeyPair,
        std::pair<CKey,CPubKey>& masternodeCollateralKeyPair,
        MasternodeTier& nMasternodeTier);
};

#endif
