// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"

#include "activemasternode.h"
#include "addrman.h"
#include <chain.h>
#include "BlockDiskAccessor.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "sync.h"
#include "Logging.h"
#include <boost/lexical_cast.hpp>
#include <main.h>
#include <init.h>
#include <wallet.h>

// keep track of the scanning errors I've seen
std::map<uint256, int> mapSeenMasternodeScanningErrors;
extern CChain chainActive;


static CAmount getCollateralAmount(MasternodeTier tier)
{
  if(tier >= MasternodeTier::COPPER && tier < MasternodeTier::INVALID)
  {
    return CMasternode::GetTierCollateralAmount(tier);
  }
  else
  {
    return static_cast<CAmount>(-1.0);
  }
}

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

static bool GetUTXOCoins(const uint256& txhash, CCoins& coins)
{
    LOCK(cs_main);
    if (!pcoinsTip->GetCoins(txhash, coins))
        return false;

    return true;
}

static bool IsCoinSpent(const COutPoint &outpoint, const CAmount expectedCollateral)
{
    CCoins coins;
    if(GetUTXOCoins(outpoint.hash, coins))
    {
        int n = outpoint.n;
        if (n < 0 || (unsigned int)n >= coins.vout.size() || coins.vout[n].IsNull()) {
            return true;
        }
        else if (coins.vout[n].nValue != expectedCollateral)
        {
            return true;
        }
        else {
            return false;
        }
    }

    return true;
}


bool GetBlockHashForScoring(uint256& hash, int nBlockHeight)
{
    const auto* tip = chainActive.Tip();
    if (tip == nullptr)
        return false;
    return GetBlockHashForScoring(hash, tip, nBlockHeight - tip->nHeight);
}

bool GetBlockHashForScoring(uint256& hash, const CBlockIndex* pindex, const int offset)
{
    if (pindex == nullptr)
        return false;

    const auto* pindexAncestor = pindex->GetAncestor(pindex->nHeight + offset - 101);
    if (pindexAncestor == nullptr)
        return false;

    hash = pindexAncestor->GetBlockHash();
    return true;
}

CMasternode::CMasternode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyMasternode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasternodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
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
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
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
    sig = mnb.sig;
    activeState = MASTERNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
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

CMasternode& CMasternode::operator=(CMasternode from)
{
    swap(*this, from);
    return *this;
}

bool CMasternode::IsBroadcastedWithin(int seconds)
{
    return (GetAdjustedTime() - sigTime) < seconds;
}

bool CMasternode::IsPingedWithin(int seconds, int64_t now)
{
    if (now == -1)
        now = GetAdjustedTime();

    if (lastPing == CMasternodePing())
        return false;

    return now - lastPing.sigTime < seconds;
}

void CMasternode::Disable()
{
    sigTime = 0;
    lastPing = CMasternodePing();
}

bool CMasternode::IsEnabled() const
{
    return activeState == MASTERNODE_ENABLED;
}

int CMasternode::GetMasternodeInputAge()
{
    if (chainActive.Tip() == NULL) return 0;

    if (cacheInputAge == 0) {
        cacheInputAge = GetInputAge(vin);
        cacheInputAgeBlock = chainActive.Tip()->nHeight;
    }

    return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
}

std::string CMasternode::Status()
{
    std::string strStatus = "ACTIVE";

    if (activeState == CMasternode::MASTERNODE_ENABLED) strStatus = "ENABLED";
    if (activeState == CMasternode::MASTERNODE_EXPIRED) strStatus = "EXPIRED";
    if (activeState == CMasternode::MASTERNODE_VIN_SPENT) strStatus = "VIN_SPENT";
    if (activeState == CMasternode::MASTERNODE_REMOVE) strStatus = "REMOVE";
    if (activeState == CMasternode::MASTERNODE_POS_ERROR) strStatus = "POS_ERROR";

    return strStatus;
}
//
// When a new masternode broadcast is sent, update our information
//
bool CMasternode::UpdateFromNewBroadcast(CMasternodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyMasternode = mnb.pubKeyMasternode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CMasternodePing() || (mnb.lastPing != CMasternodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenMasternodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
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

void CMasternode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < MASTERNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == MASTERNODE_VIN_SPENT) return;


    if (!IsPingedWithin(MASTERNODE_REMOVAL_SECONDS)) {
        activeState = MASTERNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(MASTERNODE_EXPIRATION_SECONDS)) {
        activeState = MASTERNODE_EXPIRED;
        return;
    }

    if (!unitTest) {
        if (IsCoinSpent(vin.prevout, getCollateralAmount(nTier))) {
            activeState = MASTERNODE_VIN_SPENT;
            return;
        }
    }

    activeState = MASTERNODE_ENABLED; // OK
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

int64_t CMasternode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CMasternode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        uint256 seedHash;
        if (!GetBlockHashForScoring(seedHash, BlockReading, 0))
            continue;

        auto* masternodePayees = masternodePayments.GetPayeesForScoreHash(seedHash);
        if (masternodePayees != nullptr) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (masternodePayees->HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CMasternode::GetStatus()
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

bool CMasternode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
            (IsReachable(addr) && addr.IsRoutable());
}

CMasternodeBroadcast::CMasternodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasternodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    nTier = MasternodeTier::INVALID;
}

CMasternodeBroadcast::CMasternodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMasternodeNew, const MasternodeTier nMasternodeTier, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyMasternode = pubKeyMasternodeNew;
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMasternodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    nTier = nMasternodeTier;
}

CMasternodeBroadcast::CMasternodeBroadcast(const CMasternode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyMasternode = mn.pubKeyMasternode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
    nTier = mn.nTier;
}


bool CMasternodeBroadcastFactory::checkBlockchainSync(std::string& strErrorRet, bool fOffline)
{
     if (!fOffline && !masternodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        return false;
    }
    return true;
}
bool CMasternodeBroadcastFactory::setMasternodeKeys(
    const std::string& strKeyMasternode,
    std::pair<CKey,CPubKey>& masternodeKeyPair,
    std::string& strErrorRet)
{
    if (!CObfuScationSigner::GetKeysFromSecret(strKeyMasternode, masternodeKeyPair.first, masternodeKeyPair.second)) {
        strErrorRet = strprintf("Invalid masternode key %s", strKeyMasternode);
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        return false;
    }
    return true;
}
bool CMasternodeBroadcastFactory::setMasternodeCollateralKeys(
    const std::string& txHash,
    const std::string& outputIndex,
    const std::string& service,
    bool collateralPrivKeyIsRemote,
    CTxIn& txin,
    std::pair<CKey,CPubKey>& masternodeCollateralKeyPair,
    std::string& strError)
{
    if(collateralPrivKeyIsRemote)
    {
        uint256 txid(txHash);
        uint32_t outputIdx = static_cast<uint32_t>(std::stoi(outputIndex));
        txin = CTxIn(txid,outputIdx);
        masternodeCollateralKeyPair = std::pair<CKey,CPubKey>();
        return true;
    }
    if (!pwalletMain->GetMasternodeVinAndKeys(txin, masternodeCollateralKeyPair.second, masternodeCollateralKeyPair.first, txHash, outputIndex)) {
        strError = strprintf("Could not allocate txin %s:%s for masternode %s", txHash, outputIndex, service);
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strError);
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::checkMasternodeCollateral(
    const CTxIn& txin,
    const std::string& txHash,
    const std::string& outputIndex,
    const std::string& service,
    MasternodeTier& nMasternodeTier,
    std::string& strErrorRet)
{
    nMasternodeTier = MasternodeTier::INVALID;
    auto walletTx = pwalletMain->GetWalletTx(txin.prevout.hash);
    uint256 blockHash;
    CTransaction fundingTx;
    if(walletTx || GetTransaction(txin.prevout.hash,fundingTx,blockHash,true))
    {
        auto collateralAmount = (walletTx)? walletTx->vout.at(txin.prevout.n).nValue: fundingTx.vout[txin.prevout.n].nValue;
        nMasternodeTier = CMasternode::GetTierByCollateralAmount(collateralAmount);
        if(!CMasternode::IsTierValid(nMasternodeTier))
        {
            strErrorRet = strprintf("Invalid tier selected for masternode %s, collateral value is: %d", service, collateralAmount);
            LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
            return false;
        }
    }
    else
    {
        strErrorRet = strprintf("Could not allocate txin %s:%s for masternode %s", txHash, outputIndex, service);
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::createArgumentsFromConfig(
    const CMasternodeConfig::CMasternodeEntry configEntry,
    std::string& strErrorRet,
    bool fOffline,
    bool collateralPrivKeyIsRemote,
    CTxIn& txin,
    std::pair<CKey,CPubKey>& masternodeKeyPair,
    std::pair<CKey,CPubKey>& masternodeCollateralKeyPair,
    MasternodeTier& nMasternodeTier
    )
{
    std::string strService = configEntry.getIp();
    std::string strKeyMasternode = configEntry.getPrivKey();
    std::string strTxHash = configEntry.getTxHash();
    std::string strOutputIndex = configEntry.getOutputIndex();
    //need correct blocks to send ping
    if (!checkBlockchainSync(strErrorRet,fOffline)||
        !setMasternodeKeys(strKeyMasternode,masternodeKeyPair,strErrorRet) ||
        !setMasternodeCollateralKeys(strTxHash,strOutputIndex,strService,collateralPrivKeyIsRemote,txin,masternodeCollateralKeyPair,strErrorRet) ||
        !checkMasternodeCollateral(txin,strTxHash,strOutputIndex,strService,nMasternodeTier,strErrorRet))
    {
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::Create(const CMasternodeConfig::CMasternodeEntry configEntry,
                    CPubKey pubkeyCollateralAddress,
                    std::string& strErrorRet,
                    CMasternodeBroadcast& mnbRet,
                    bool fOffline)
{
    const bool collateralPrivateKeyIsRemote = true;
    const bool deferRelay = true;
    CTxIn txin;
    std::pair<CKey,CPubKey> masternodeCollateralKeyPair;
    std::pair<CKey,CPubKey> masternodeKeyPair;
    MasternodeTier nMasternodeTier;

    if(!createArgumentsFromConfig(
        configEntry,
        strErrorRet,
        fOffline,
        collateralPrivateKeyIsRemote,
        txin,
        masternodeKeyPair,
        masternodeCollateralKeyPair,
        nMasternodeTier))
    {
        return false;
    }

    createWithoutSignatures(
        txin,
        CService(configEntry.getIp()),
        pubkeyCollateralAddress,
        masternodeKeyPair.second,
        nMasternodeTier,
        deferRelay,
        mnbRet);

    if(!signPing(masternodeKeyPair.first,masternodeKeyPair.second,mnbRet.lastPing,strErrorRet))
    {
        mnbRet = CMasternodeBroadcast();
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::Create(
    const CMasternodeConfig::CMasternodeEntry configEntry,
    std::string& strErrorRet,
    CMasternodeBroadcast& mnbRet,
    bool fOffline,
    bool deferRelay)
{
    const bool collateralPrivateKeyIsRemote = false;
    std::string strService = configEntry.getIp();
    std::string strKeyMasternode = configEntry.getPrivKey();
    std::string strTxHash = configEntry.getTxHash();
    std::string strOutputIndex = configEntry.getOutputIndex();

    CTxIn txin;
    std::pair<CKey,CPubKey> masternodeCollateralKeyPair;
    std::pair<CKey,CPubKey> masternodeKeyPair;
    MasternodeTier nMasternodeTier;

    if(!createArgumentsFromConfig(
        configEntry,
        strErrorRet,
        fOffline,
        collateralPrivateKeyIsRemote,
        txin,
        masternodeKeyPair,
        masternodeCollateralKeyPair,
        nMasternodeTier))
    {
        return false;
    }

    return Create(txin,
                CService(strService),
                masternodeCollateralKeyPair.first,
                masternodeCollateralKeyPair.second,
                masternodeKeyPair.first,
                masternodeKeyPair.second,
                nMasternodeTier,
                strErrorRet,
                mnbRet,
                deferRelay);
}

bool CMasternodeBroadcastFactory::signPing(
    CKey keyMasternodeNew,
    CPubKey pubKeyMasternodeNew,
    CMasternodePing& mnp,
    std::string& strErrorRet)
{
    if (!mnp.Sign(keyMasternodeNew, pubKeyMasternodeNew))
    {
        strErrorRet = strprintf("Failed to sign ping, masternode=%s", mnp.vin.prevout.hash.ToString());
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::signBroadcast(
    CKey keyCollateralAddressNew,
    CMasternodeBroadcast& mnb,
    std::string& strErrorRet)
{
    if (!mnb.Sign(keyCollateralAddressNew))
    {
        strErrorRet = strprintf("Failed to sign broadcast, masternode=%s", mnb.vin.prevout.hash.ToString());
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        mnb = CMasternodeBroadcast();
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::provideSignatures(
    CKey keyMasternodeNew,
    CPubKey pubKeyMasternodeNew,
    CKey keyCollateralAddressNew,
    CMasternodeBroadcast& mnb,
    std::string& strErrorRet)
{
    if(!signPing(keyMasternodeNew,pubKeyMasternodeNew,mnb.lastPing,strErrorRet))
    {
        return false;
    }
    if (!signBroadcast(keyCollateralAddressNew,mnb,strErrorRet))
    {
        return false;
    }
    return true;
}

void CMasternodeBroadcastFactory::createWithoutSignatures(
    CTxIn txin,
    CService service,
    CPubKey pubKeyCollateralAddressNew,
    CPubKey pubKeyMasternodeNew,
    const MasternodeTier nMasternodeTier,
    bool deferRelay,
    CMasternodeBroadcast& mnbRet)
{
    LogPrint("masternode", "CMasternodeBroadcastFactory::createWithoutSignatures -- pubKeyCollateralAddressNew = %s, pubKeyMasternodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyMasternodeNew.GetID().ToString());

    CMasternodePing mnp = (deferRelay)? CMasternodePing::createDelayedMasternodePing(txin): CMasternodePing(txin);
    mnbRet = CMasternodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMasternodeNew, nMasternodeTier, PROTOCOL_VERSION);
    mnbRet.lastPing = mnp;
}

bool CMasternodeBroadcastFactory::Create(
    CTxIn txin,
    CService service,
    CKey keyCollateralAddressNew,
    CPubKey pubKeyCollateralAddressNew,
    CKey keyMasternodeNew,
    CPubKey pubKeyMasternodeNew,
    const MasternodeTier nMasternodeTier,
    std::string& strErrorRet,
    CMasternodeBroadcast& mnbRet,
    bool deferRelay)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    createWithoutSignatures(
        txin,service,pubKeyCollateralAddressNew,pubKeyMasternodeNew,nMasternodeTier,deferRelay,mnbRet);

    if(!provideSignatures(keyMasternodeNew,pubKeyMasternodeNew,keyCollateralAddressNew,mnbRet,strErrorRet))
    {
        return false;
    }

    return true;
}

bool CMasternodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("%s : mnb - Signature rejected, too far into the future %s\n", __func__, vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if(!CMasternode::IsTierValid(static_cast<MasternodeTier>(nTier))) {
        LogPrintf("%s : mnb - Invalid tier: %d\n", __func__, static_cast<int>(nTier));
        nDos = 20;
        return false;
    }

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
        LogPrint("masternode","mnb - ignoring outdated Masternode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("%s : mnb - pubkey the wrong size\n", __func__);
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyMasternode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("%s : mnb - pubkey2 the wrong size\n", __func__);
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("masternode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!CObfuScationSigner::VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrintf("%s : - Got bad Masternode address signature\n", __func__);
        nDos = 100;
        return false;
    }

    //search existing Masternode list, this is where we update existing Masternodes with new mnb broadcasts
    CMasternode* pmn = mnodeman.Find(vin);

    // no such masternode, nothing to update
    if (pmn == NULL)
        return true;
    else {
        // this broadcast older than we have, it's bad.
        if (pmn->sigTime > sigTime) {
            LogPrint("masternode","mnb - Bad sigTime %d for Masternode %s (existing broadcast is at %d)\n",
                     sigTime, vin.prevout.hash.ToString(), pmn->sigTime);
            return false;
        }
        // masternode is not enabled yet/already, nothing to update
        if (!pmn->IsEnabled()) return true;
    }

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(MASTERNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("masternode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        masternodeSync.AddedMasternodeList(GetHash());
    }

    return true;
}

bool CMasternodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a masternode with the same vin (i.e. already activated) and this mnb is ours (matches our Masternode privkey)
    // so nothing to do here for us
    if (fMasterNode && vin.prevout == activeMasternode.vin.prevout && pubKeyMasternode == activeMasternode.pubKeyMasternode)
        return true;

    // search existing Masternode list
    CMasternode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this masternode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    if (IsCoinSpent(vin.prevout, getCollateralAmount(nTier) )) {
        LogPrint("masternode", "mnb - coin is already spent\n");
        return false;
    }


    LogPrint("masternode", "mnb - Accepted Masternode entry\n");

    if (GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS) {
        LogPrint("masternode","mnb - Input must have at least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenMasternodeBroadcast.erase(GetHash());
        masternodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 PIV tx got MASTERNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 DIVI tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1]; // block where tx got MASTERNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("masternode","mnb - Bad sigTime %d for Masternode %s (%i conf block is at %d)\n",
                     sigTime, vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("masternode","mnb - Got NEW Masternode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CMasternode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Masternode privkey, then we've been remotely activated
    if (pubKeyMasternode == activeMasternode.pubKeyMasternode && protocolVersion == PROTOCOL_VERSION) {
        activeMasternode.EnableHotColdMasterNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CMasternodeBroadcast::Relay() const
{
    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

std::string CMasternodeBroadcast::getMessageToSign() const
{
    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);
    return strMessage;
}

bool CMasternodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = getMessageToSign();

    if (!CObfuScationSigner::SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrint("masternode","CMasternodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!CObfuScationSigner::VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrint("masternode","CMasternodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CMasternodePing::CMasternodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CMasternodePing::CMasternodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}

CMasternodePing CMasternodePing::createDelayedMasternodePing(CTxIn& newVin)
{
    CMasternodePing ping;
    const int64_t offsetTimeBy45BlocksInSeconds = 60 * 45;
    ping.vin = newVin;
    auto block = chainActive[chainActive.Height() -12];
    ping.blockHash = block->GetBlockHash();
    ping.sigTime = std::max(block->GetBlockTime() + offsetTimeBy45BlocksInSeconds, GetAdjustedTime());
    ping.vchSig = std::vector<unsigned char>();
    LogPrint("masternode","mnp - relay block-time & sigtime: %d vs. %d\n", block->GetBlockTime(), ping.sigTime);

    return ping;
}

std::string CMasternodePing::getMessageToSign() const
{
    return vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
}

bool CMasternodePing::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = getMessageToSign();

    if (!CObfuScationSigner::SignMessage(strMessage, errorMessage, vchSig, keyMasternode)) {
        LogPrint("masternode","CMasternodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!CObfuScationSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint("masternode","CMasternodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CMasternodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("masternode","CMasternodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("masternode","CMasternodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    LogPrint("masternode","CMasternodePing::CheckAndUpdate - New Ping - %s - %lli\n", blockHash.ToString(), sigTime);

    // see if we have this Masternode
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint("masternode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this masternode or
        // last ping was more then MASTERNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(MASTERNODE_MIN_MNP_SECONDS - 60, sigTime)) {
            std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

            std::string errorMessage = "";
            if (!CObfuScationSigner::VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
                LogPrint("masternode","CMasternodePing::CheckAndUpdate - Got bad Masternode address signature %s\n", vin.prevout.hash.ToString());
                nDos = 33;
                return false;
            }

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("masternode","CMasternodePing::CheckAndUpdate - Masternode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Masternode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("masternode","CMasternodePing::CheckAndUpdate - Masternode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
            CMasternodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
                mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("masternode", "CMasternodePing::CheckAndUpdate - Masternode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("masternode", "CMasternodePing::CheckAndUpdate - Masternode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("masternode", "CMasternodePing::CheckAndUpdate - Couldn't find compatible Masternode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CMasternodePing::Relay()
{
    CInv inv(MSG_MASTERNODE_PING, GetHash());
    RelayInv(inv);
}

int GetInputAge(CTxIn& vin)
{
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewMemPool viewMempool(pcoinsTip, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        const CCoins* coins = view.AccessCoins(vin.prevout.hash);

        if (coins) {
            if (coins->nHeight < 0) return 0;
            return (chainActive.Tip()->nHeight + 1) - coins->nHeight;
        } else
            return -1;
    }
}
