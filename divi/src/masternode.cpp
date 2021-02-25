// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"

#include <sync.h>
#include <Logging.h>
#include <boost/lexical_cast.hpp>
#include <timedata.h>
#include <script/standard.h>
#include <chainparams.h>

CAmount CMasternode::GetTierCollateralAmount(const MasternodeTier tier)
{
    const auto& collateralMap = Params().MasternodeCollateralMap();
    const auto mit = collateralMap.find(tier);
    if (mit == collateralMap.end())
        return 0;
    return mit->second;
}

static size_t GetHashRoundsForTierMasternodes(MasternodeTier tier)
{
    switch(tier)
    {
    case MasternodeTier::COPPER:   return 20;
    case MasternodeTier::SILVER:   return 63;
    case MasternodeTier::GOLD:     return 220;
    case MasternodeTier::PLATINUM: return 690;
    case MasternodeTier::DIAMOND:  return 2400;
    case MasternodeTier::INVALID: break;
    }

    return 0;
}

CMasternode::CMasternode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyMasternode = CPubKey();
    signature = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasternodePing();
    collateralBlock.SetNull();
    allowFreeTx = true;
    nActiveState = MASTERNODE_ENABLED;
    protocolVersion = PROTOCOL_VERSION;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nTier = MasternodeTier::INVALID;
}

CMasternode::CMasternode(const CMasternode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyMasternode = other.pubKeyMasternode;
    signature = other.signature;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    collateralBlock = other.collateralBlock;
    allowFreeTx = other.allowFreeTx;
    nActiveState = MASTERNODE_ENABLED;
    protocolVersion = other.protocolVersion;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nTier = other.nTier;
}

CMasternode::CMasternode(const CMasternodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyMasternode = mnb.pubKeyMasternode;
    signature = mnb.signature;
    activeState = MASTERNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    collateralBlock = mnb.collateralBlock;
    allowFreeTx = true;
    nActiveState = MASTERNODE_ENABLED;
    protocolVersion = mnb.protocolVersion;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nTier = mnb.nTier;
}

void CMasternode::swap(CMasternode& first, CMasternode& second) // nothrow
{
    // enable ADL (not necessary in our case, but good practice)
    using std::swap;

    // by swapping the members of two classes,
    // the two classes are effectively swapped
    swap(first.vin, second.vin);
    swap(first.addr, second.addr);
    swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
    swap(first.pubKeyMasternode, second.pubKeyMasternode);
    swap(first.signature, second.signature);
    swap(first.activeState, second.activeState);
    swap(first.sigTime, second.sigTime);
    swap(first.lastPing, second.lastPing);
    swap(first.collateralBlock, second.collateralBlock);
    swap(first.allowFreeTx, second.allowFreeTx);
    swap(first.protocolVersion, second.protocolVersion);
    swap(first.nScanningErrorCount, second.nScanningErrorCount);
    swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    swap(first.nTier, second.nTier);
}

CMasternode& CMasternode::operator=(CMasternode from)
{
    swap(*this, from);
    return *this;
}

bool CMasternode::IsEnabled() const
{
    return activeState == MASTERNODE_ENABLED;
}

std::string CMasternode::Status() const
{
    std::string strStatus = "ACTIVE";

    if (activeState == CMasternode::MASTERNODE_ENABLED) strStatus = "ENABLED";
    if (activeState == CMasternode::MASTERNODE_EXPIRED) strStatus = "EXPIRED";
    if (activeState == CMasternode::MASTERNODE_VIN_SPENT) strStatus = "VIN_SPENT";
    if (activeState == CMasternode::MASTERNODE_REMOVE) strStatus = "REMOVE";
    if (activeState == CMasternode::MASTERNODE_POS_ERROR) strStatus = "POS_ERROR";

    return strStatus;
}

CScript CMasternode::GetPaymentScript() const
{
    const CTxDestination dest(pubKeyCollateralAddress.GetID());
    return GetScriptForDestination(dest);
}

static uint256 CalculateScoreHelper(CHashWriter hashWritter, int round)
{
    hashWritter << round;
    return hashWritter.GetHash();
}

//
// Deterministically calculate a given "score" for a Masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CMasternode::CalculateScore(const uint256& seedHash) const
{
    const uint256 aux = vin.prevout.hash + vin.prevout.n;
    const size_t nHashRounds = GetHashRoundsForTierMasternodes(static_cast<MasternodeTier>(nTier));

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << seedHash;
    ss << aux;

    uint256 r;
    for(size_t i = 0; i < nHashRounds; ++i) {
        r = std::max(CalculateScoreHelper(ss, i), r);
    }

    return r;
}

MasternodeTier CMasternode::GetTierByCollateralAmount(const CAmount nCollateral)
{
    for (const auto& entry : Params().MasternodeCollateralMap())
        if (entry.second == nCollateral)
            return entry.first;
    return MasternodeTier::INVALID;
}

bool CMasternode::IsTierValid(MasternodeTier tier)
{
    switch(tier)
    {
    case MasternodeTier::COPPER:
    case MasternodeTier::SILVER:
    case MasternodeTier::GOLD:
    case MasternodeTier::PLATINUM:
    case MasternodeTier::DIAMOND: return true;
    case MasternodeTier::INVALID: break;
    }

    return false;
}

std::string CMasternode::TierToString(MasternodeTier tier)
{
    switch(tier)
    {
    case MasternodeTier::COPPER: return "COPPER";
    case MasternodeTier::SILVER: return "SILVER";
    case MasternodeTier::GOLD: return "GOLD";
    case MasternodeTier::PLATINUM: return "PLATINUM";
    case MasternodeTier::DIAMOND: return "DIAMOND";
    case MasternodeTier::INVALID: break;
    }

    return "INVALID";
}

int64_t CMasternode::DeterministicTimeOffset() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    return hash.GetCompact(false) % 150;
}

std::string CMasternode::GetStatus() const
{
    switch (nActiveState) {
    case CMasternode::MASTERNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CMasternode::MASTERNODE_ENABLED:
        return "ENABLED";
    case CMasternode::MASTERNODE_EXPIRED:
        return "EXPIRED";
    case CMasternode::MASTERNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CMasternode::MASTERNODE_REMOVE:
        return "REMOVE";
    case CMasternode::MASTERNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CMasternode::MASTERNODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CMasternode::IsValidNetAddr() const
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
            (IsReachable(addr) && addr.IsRoutable());
}

CMasternodeBroadcast::CMasternodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMasternodeNew, const MasternodeTier nMasternodeTier, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyMasternode = pubKeyMasternodeNew;
    protocolVersion = protocolVersionIn;
    nTier = nMasternodeTier;
}

CMasternodeBroadcast::CMasternodeBroadcast(const CMasternode& mn)
  : CMasternode(mn)
{}

void CMasternodeBroadcast::Relay() const
{
    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

std::string CMasternodeBroadcast::getMessageToSign() const
{
    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());

    return addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);
}

uint256 CMasternodeBroadcast::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << sigTime;
    ss << pubKeyCollateralAddress;
    return ss.GetHash();
}
