// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#include "masternodeconfig.h"

#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MASTERNODE_PING_SECONDS (5 * 60)
#define MASTERNODE_EXPIRATION_SECONDS (120 * 60)
#define MASTERNODE_REMOVAL_SECONDS (130 * 60)
#define MASTERNODE_CHECK_SECONDS 5

using namespace std;

class CMasternode;
class CMasternodeBroadcast;
class CMasternodeBroadcastFactory;
class CMasternodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);

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
    static CMasternodePing createDelayedMasternodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true);
    std::string getMessageToSign() const;
    bool Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode);
    void Relay();

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
    int64_t lastTimeChecked;

public:
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

    enum Tier {
        MASTERNODE_TIER_COPPER,
        MASTERNODE_TIER_SILVER,
        MASTERNODE_TIER_GOLD,
        MASTERNODE_TIER_PLATINUM,
        MASTERNODE_TIER_DIAMOND,
        MASTERNODE_TIER_INVALID
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    int nTier;
    CMasternodePing lastPing;

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);


    void swap(CMasternode& first, CMasternode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyMasternode, second.pubKeyMasternode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
        swap(first.nTier, second.nTier);
    }

    CMasternode& operator=(CMasternode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasternode& a, const CMasternode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMasternode& a, const CMasternode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod = 1, int64_t nBlockHeight = 0);

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
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
        READWRITE(nTier);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CMasternodeBroadcast &mnb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CMasternodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CMasternodePing();
    }

    bool IsEnabled()
    {
        return activeState == MASTERNODE_ENABLED;
    }

    int GetMasternodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    static CAmount GetTierCollateralAmount(Tier tier);
    static Tier GetTierByCollateralAmount(CAmount nCollateral);
    static bool IsTierValid(Tier tier);
    static string TierToString(Tier tier);

    std::string GetStatus();

    std::string Status()
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CMasternode::MASTERNODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CMasternode::MASTERNODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CMasternode::MASTERNODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CMasternode::MASTERNODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CMasternode::MASTERNODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:
    CMasternodeBroadcast();
    CMasternodeBroadcast(
        CService newAddr, 
        CTxIn newVin, 
        CPubKey pubKeyCollateralAddress, 
        CPubKey pubKeyMasternode, 
        Tier nMasternodeTier, 
        int protocolVersionIn);
    CMasternodeBroadcast(const CMasternode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
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
        READWRITE(nTier);
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
    
    static void createWithoutSignatures(
        CTxIn txin, 
        CService service,
        CPubKey pubKeyCollateralAddressNew, 
        CPubKey pubKeyMasternodeNew, 
        CMasternode::Tier nMasternodeTier,
        bool deferRelay,
        CMasternodeBroadcast& mnbRet);

private:
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
                        CMasternode::Tier nMasternodeTier,
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
        CTxIn& txin,
        std::pair<CKey,CPubKey>& masternodeCollateralKeyPair,
        std::string& error);
    static bool checkMasternodeCollateral(
        const CTxIn& txin,
        const std::string& txHash, 
        const std::string& outputIndex,
        const std::string& service,
        CMasternode::Tier& nMasternodeTier,
        std::string& strErrorRet);
    static bool checkNetworkPort(
        const std::string& strService,
        std::string& strErrorRet);
};

#endif
