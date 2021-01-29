// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"

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
#include <utiltime.h>
#include <WalletTx.h>
#include <masternode-sync.h>

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

bool CMasternode::IsCoinSpent(const COutPoint &outpoint, const MasternodeTier mnTier)
{
    CAmount expectedCollateral = getCollateralAmount(mnTier);
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

bool CMasternode::IsBroadcastedWithin(int seconds) const
{
    return (GetAdjustedTime() - sigTime) < seconds;
}

bool CMasternode::TimeSinceLastPingIsWithin(int seconds, int64_t now) const
{
    if (now == -1)
        now = GetAdjustedTime();

    if (lastPing == CMasternodePing())
        return false;

    return now - lastPing.sigTime < seconds;
}
bool CMasternode::IsTooEarlyToReceivePingUpdate(int64_t now) const
{
    return TimeSinceLastPingIsWithin(MASTERNODE_MIN_MNP_SECONDS - 60, now);
}
bool CMasternode::IsTooEarlyToSendPingUpdate(int64_t now) const
{
    return TimeSinceLastPingIsWithin(MASTERNODE_PING_SECONDS, now);
}

bool CMasternode::IsEnabled() const
{
    return activeState == MASTERNODE_ENABLED;
}

int CMasternode::GetMasternodeInputAge() const
{
    LOCK(cs_main);

    const auto* pindex = GetCollateralBlock();
    if (pindex == nullptr)
        return 0;

    assert(chainActive.Contains(pindex));

    const unsigned tipHeight = chainActive.Height();
    assert(tipHeight >= pindex->nHeight);

    return tipHeight - pindex->nHeight + 1;
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

void CMasternode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < MASTERNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == MASTERNODE_VIN_SPENT) return;


    if (!TimeSinceLastPingIsWithin(MASTERNODE_REMOVAL_SECONDS)) {
        activeState = MASTERNODE_REMOVE;
        return;
    }

    if (!TimeSinceLastPingIsWithin(MASTERNODE_EXPIRATION_SECONDS)) {
        activeState = MASTERNODE_EXPIRED;
        return;
    }

    if (IsCoinSpent(vin.prevout, nTier)) {
        activeState = MASTERNODE_VIN_SPENT;
        return;
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


bool CMasternodeBroadcastFactory::checkBlockchainSync(CMasternodeSync& masternodeSynchronization, std::string& strErrorRet, bool fOffline)
{
     if (!fOffline && !masternodeSynchronization.IsBlockchainSynced()) {
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
    CMasternodeSync& masternodeSynchronization,
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
    if (!checkBlockchainSync(masternodeSynchronization,strErrorRet,fOffline)||
        !setMasternodeKeys(strKeyMasternode,masternodeKeyPair,strErrorRet) ||
        !setMasternodeCollateralKeys(strTxHash,strOutputIndex,strService,collateralPrivKeyIsRemote,txin,masternodeCollateralKeyPair,strErrorRet) ||
        !checkMasternodeCollateral(txin,strTxHash,strOutputIndex,strService,nMasternodeTier,strErrorRet))
    {
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::Create(
    CMasternodeSync& masternodeSynchronization,
    const CMasternodeConfig::CMasternodeEntry configEntry,
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
        masternodeSynchronization,
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
    CMasternodeSync& masternodeSynchronization,
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
        masternodeSynchronization,
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
    const CKey& keyMasternodeNew,
    const CPubKey& pubKeyMasternodeNew,
    CMasternodePing& mnp,
    std::string& strErrorRet)
{
    if(!CObfuScationSigner::SignAndVerify<CMasternodePing>(mnp,keyMasternodeNew,pubKeyMasternodeNew,strErrorRet))
    {
        strErrorRet = strprintf("Failed to sign ping, masternode=%s", mnp.vin.prevout.hash.ToString());
        LogPrint("masternode","%s -- %s\n",__func__, strErrorRet);
        return false;
    }
    return true;
}

bool CMasternodeBroadcastFactory::signBroadcast(
    CKey keyCollateralAddressNew,
    CMasternodeBroadcast& mnb,
    std::string& strErrorRet)
{
    if (! CObfuScationSigner::SignAndVerify<CMasternodeBroadcast>(mnb,keyCollateralAddressNew,mnb.pubKeyCollateralAddress,strErrorRet))
    {
        strErrorRet = strprintf("Failed to sign broadcast, masternode=%s", mnb.vin.prevout.hash.ToString());
        LogPrint("masternode","%s -- %s\n", __func__, strErrorRet);
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

namespace
{

CMasternodePing createDelayedMasternodePing(const CMasternodeBroadcast& mnb)
{
    CMasternodePing ping;
    const int64_t offsetTimeBy45BlocksInSeconds = 60 * 45;
    ping.vin = mnb.vin;
    const int depthOfTx = mnb.GetMasternodeInputAge();
    const int offset = std::min( std::max(0, depthOfTx), 12 );
    const auto* block = chainActive[chainActive.Height() - offset];
    ping.blockHash = block->GetBlockHash();
    ping.sigTime = std::max(block->GetBlockTime() + offsetTimeBy45BlocksInSeconds, GetAdjustedTime());
    ping.signature = std::vector<unsigned char>();
    LogPrint("masternode","mnp - relay block-time & sigtime: %d vs. %d\n", block->GetBlockTime(), ping.sigTime);

    return ping;
}

} // anonymous namespace

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

    mnbRet = CMasternodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMasternodeNew, nMasternodeTier, PROTOCOL_VERSION);
    const CMasternodePing mnp = (deferRelay
                                    ? createDelayedMasternodePing(mnbRet)
                                    : CMasternodePing(txin));
    mnbRet.lastPing = mnp;
    mnbRet.sigTime = mnp.sigTime;
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

const CBlockIndex* CMasternode::GetCollateralBlock() const
{
    LOCK(cs_main);

    if (!collateralBlock.IsNull()) {
        const auto mi = mapBlockIndex.find(collateralBlock);
        if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second))
            return mi->second;
    }

    uint256 hashBlock;
    CTransaction tx;
    if (!GetTransaction(vin.prevout.hash, tx, hashBlock, true)) {
        collateralBlock.SetNull();
        return nullptr;
    }

    const auto mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end() || mi->second == nullptr) {
        collateralBlock.SetNull();
        return nullptr;
    }

    if (!chainActive.Contains(mi->second)) {
        collateralBlock.SetNull();
        return nullptr;
    }

    collateralBlock = hashBlock;
    return mi->second;
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

    return addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);
}

uint256 CMasternodeBroadcast::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << sigTime;
    ss << pubKeyCollateralAddress;
    return ss.GetHash();
}

CMasternodePing::CMasternodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    signature = std::vector<unsigned char>();
}

CMasternodePing::CMasternodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
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