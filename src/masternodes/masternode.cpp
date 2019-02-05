// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternode.h>
#include <addrman.h>
#include <masternodes/masternodeman.h>
#include <masternodes/masternode-payments.h>
#include <masternodes/masternode-sync.h>
#include <masternodes/activemasternode.h>
#include <messagesigner.h>
#include <sync.h>
#include <key_io.h>
#include <netbase.h>
#include <script/script.h>
#include <script/standard.h>
#include <chainparams.h>
#include <shutdown.h>
#include <wallet/wallet.h>
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenMasternodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;


const int TIER_COPPER_BASE_COLLATERAL   = 100000;
const int TIER_SILVER_BASE_COLLATERAL   = 300000;
const int TIER_GOLD_BASE_COLLATERAL     = 1000000;
const int TIER_PLATINUM_BASE_COLLATERAL = 3000000;
const int TIER_DIAMOND_BASE_COLLATERAL  = 10000000;

static size_t GetHashRoundsForTierMasternodes(CMasternode::Tier tier)
{
    switch(tier)
    {
    case CMasternode::MASTERNODE_TIER_COPPER:   return 20;
    case CMasternode::MASTERNODE_TIER_SILVER:   return 63;
    case CMasternode::MASTERNODE_TIER_GOLD:     return 220;
    case CMasternode::MASTERNODE_TIER_PLATINUM: return 690;
    case CMasternode::MASTERNODE_TIER_DIAMOND:  return 2400;
    case CMasternode::MASTERNODE_TIER_INVALID: break;
    }

    return 0;
}

static bool GetUTXOCoin(const COutPoint& outpoint, Coin& coin)
{
    LOCK(cs_main);
    if (!pcoinsTip->GetCoin(outpoint, coin))
        return false;
    if (coin.IsSpent())
        return false;
    return true;
}

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
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
    nTier = MASTERNODE_TIER_INVALID;
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

//
// When a new masternode broadcast is sent, update our information
//
bool CMasternode::UpdateFromNewBroadcast(CMasternodeBroadcast& mnb, CConnman &connman)
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
        if (mnb.lastPing == CMasternodePing() || (mnb.lastPing != CMasternodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false, connman))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenMasternodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

static arith_uint256 CalculateScoreHelper(CHashWriter hashWritter, int round)
{
    hashWritter << round;
    return UintToArith256(hashWritter.GetHash());
}

//
// Deterministically calculate a given "score" for a Masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CMasternode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == nullptr)
        return 0;

    uint256 hash;
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint(BCLog::MASTERNODE,"CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    size_t nHashRounds = GetHashRoundsForTierMasternodes(static_cast<CMasternode::Tier>(nTier));

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    ss << aux;

    arith_uint256 r;

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
        Coin coin;
        if (!GetUTXOCoin(vin.prevout, coin)) {
            activeState = MASTERNODE_VIN_SPENT;
            return;
        }
    }

    activeState = MASTERNODE_ENABLED; // OK
}

CAmount CMasternode::GetTierCollateralAmount(CMasternode::Tier tier)
{
    switch(tier)
    {
    case MASTERNODE_TIER_COPPER:   return TIER_COPPER_BASE_COLLATERAL * COIN;
    case MASTERNODE_TIER_SILVER:   return TIER_SILVER_BASE_COLLATERAL * COIN;
    case MASTERNODE_TIER_GOLD:     return TIER_GOLD_BASE_COLLATERAL * COIN;
    case MASTERNODE_TIER_PLATINUM: return TIER_PLATINUM_BASE_COLLATERAL * COIN;
    case MASTERNODE_TIER_DIAMOND:  return TIER_DIAMOND_BASE_COLLATERAL * COIN;
    case MASTERNODE_TIER_INVALID: break;
    }

    return 0;
}

CMasternode::Tier CMasternode::GetTierByCollateralAmount(CAmount nCollateral)
{
    if(TIER_COPPER_BASE_COLLATERAL * COIN == nCollateral) {
        return MASTERNODE_TIER_COPPER;
    }
    else if(TIER_SILVER_BASE_COLLATERAL * COIN == nCollateral) {
        return MASTERNODE_TIER_SILVER;
    }
    else if(TIER_GOLD_BASE_COLLATERAL * COIN == nCollateral) {
        return MASTERNODE_TIER_GOLD;
    }
    else if(TIER_PLATINUM_BASE_COLLATERAL * COIN == nCollateral) {
        return MASTERNODE_TIER_PLATINUM;
    }
    else if(TIER_DIAMOND_BASE_COLLATERAL * COIN == nCollateral) {
        return MASTERNODE_TIER_DIAMOND;
    }

    return MASTERNODE_TIER_INVALID;
}

bool CMasternode::IsTierValid(CMasternode::Tier tier)
{
    switch(tier)
    {
    case MASTERNODE_TIER_COPPER:
    case MASTERNODE_TIER_SILVER:
    case MASTERNODE_TIER_GOLD:
    case MASTERNODE_TIER_PLATINUM:
    case MASTERNODE_TIER_DIAMOND: return true;
    case MASTERNODE_TIER_INVALID: break;
    }

    return false;
}

std::string CMasternode::TierToString(CMasternode::Tier tier)
{
    switch(tier)
    {
    case MASTERNODE_TIER_COPPER: return "COPPER";
    case MASTERNODE_TIER_SILVER: return "SILVER";
    case MASTERNODE_TIER_GOLD: return "GOLD";
    case MASTERNODE_TIER_PLATINUM: return "PLATINUM";
    case MASTERNODE_TIER_DIAMOND: return "DIAMOND";
    case MASTERNODE_TIER_INVALID: break;
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
    arith_uint256 hash = UintToArith256(ss.GetHash());

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
    arith_uint256 hash = UintToArith256(ss.GetHash());

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

        if (masternodePayments.mapMasternodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (masternodePayments.mapMasternodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
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
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
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
    nTier = MASTERNODE_TIER_INVALID;
}

CMasternodeBroadcast::CMasternodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMasternodeNew, Tier nMasternodeTier, int protocolVersionIn)
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

bool CMasternodeBroadcast::Create(CWallet &wallet, std::string strService, std::string strKeyMasternode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CMasternodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyMasternodeNew;
    CKey keyMasternodeNew;

    //need correct blocks to send ping
    if (!fOffline && !masternodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!CMessageSigner::GetKeysFromSecret(strKeyMasternode, keyMasternodeNew, pubKeyMasternodeNew)) {
        strErrorRet = strprintf("Invalid masternode key %s", strKeyMasternode);
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!wallet.GetMasternodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for masternode %s", strTxHash, strOutputIndex, strService);
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    Tier nMasternodeTier = Tier::MASTERNODE_TIER_INVALID;
    if(auto walletTx = wallet.GetWalletTx(txin.prevout.hash))
    {
        auto collateralAmount = walletTx->tx->vout.at(txin.prevout.n).nValue;
        nMasternodeTier = GetTierByCollateralAmount(collateralAmount);
        if(!IsTierValid(nMasternodeTier))
        {
            strErrorRet = strprintf("Invalid tier selected for masternode %s, collateral value is: %d", strService, collateralAmount);
            LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    }
    else
    {
        strErrorRet = strprintf("Could not allocate txin %s:%s for masternode %s", strTxHash, strOutputIndex, strService);
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }


    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return error("Invalid address %s for masternode.", strService);
    int mainnetDefaultPort = CreateChainParams(CBaseChainParams::MAIN)->GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for masternode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for masternode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, service, keyCollateralAddressNew, pubKeyCollateralAddressNew, keyMasternodeNew, pubKeyMasternodeNew, nMasternodeTier, strErrorRet, mnbRet);
}

bool CMasternodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyMasternodeNew, CPubKey pubKeyMasternodeNew, Tier nMasternodeTier, std::string& strErrorRet, CMasternodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint(BCLog::MASTERNODE, "CMasternodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyMasternodeNew.GetID() = %s\n",
             EncodeDestination(pubKeyCollateralAddressNew.GetID()),
             pubKeyMasternodeNew.GetID().ToString());

    CMasternodePing mnp(txin);
    if (!mnp.Sign(keyMasternodeNew, pubKeyMasternodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, masternode=%s", txin.prevout.hash.ToString());
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMasternodeBroadcast();
        return false;
    }

    mnbRet = CMasternodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMasternodeNew, nMasternodeTier, PROTOCOL_VERSION);

#if 0
    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address %s, masternode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMasternodeBroadcast();
        return false;
    }
#endif

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, masternode=%s", txin.prevout.hash.ToString());
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMasternodeBroadcast();
        return false;
    }

    return true;
}

bool CMasternodeBroadcast::CheckAndUpdate(int& nDos, CConnman &connman)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("%s : mnb - Signature rejected, too far into the future %s\n", __func__, vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if(!CMasternode::IsTierValid(static_cast<CMasternode::Tier>(nTier))) {
        LogPrintf("%s : mnb - Invalid tier: %d\n", __func__, nTier);
        nDos = 20;
        return false;
    }

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (protocolVersion < masternodePayments.GetMinMasternodePaymentsProto()) {
        LogPrint(BCLog::MASTERNODE,"mnb - ignoring outdated Masternode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
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
        LogPrint(BCLog::MASTERNODE,"mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress.GetID(), sig, strMessage, errorMessage)) {
        LogPrintf("%s : - Got bad Masternode address signature\n", __func__);
        nDos = 100;
        return false;
    }

    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 51472) return false;
    } else if (addr.GetPort() == 51474)
        return false;

    //search existing Masternode list, this is where we update existing Masternodes with new mnb broadcasts
    CMasternode* pmn = mnodeman.Find(vin);

    // no such masternode, nothing to update
    if (pmn == NULL)
        return true;
    else {
        // this broadcast older than we have, it's bad.
        if (pmn->sigTime > sigTime) {
            LogPrint(BCLog::MASTERNODE,"mnb - Bad sigTime %d for Masternode %s (existing broadcast is at %d)\n",
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
        LogPrint(BCLog::MASTERNODE,"mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast(*this, connman)) {
            pmn->Check();
            if (pmn->IsEnabled())
                Relay(connman);
        }
        masternodeSync.AddedMasternodeList(GetHash());
    }

    return true;
}

bool CMasternodeBroadcast::CheckInputsAndAdd(int& nDoS, CConnman &connman)
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

    Coin coin;
    if (!GetUTXOCoin(vin.prevout, coin)) {
        LogPrintf("mnb - coin is already spent\n");
        return false;
    }


    LogPrint(BCLog::MASTERNODE, "mnb - Accepted Masternode entry\n");

    if (GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS) {
        LogPrint(BCLog::MASTERNODE,"mnb - Input must have at least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenMasternodeBroadcast.erase(GetHash());
        masternodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 PIV tx got MASTERNODE_MIN_CONFIRMATIONS
    uint256 hashBlock;
    CTransactionRef tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 PIVX tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1]; // block where tx got MASTERNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint(BCLog::MASTERNODE,"mnb - Bad sigTime %d for Masternode %s (%i conf block is at %d)\n",
                     sigTime, vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint(BCLog::MASTERNODE,"mnb - Got NEW Masternode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CMasternode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Masternode privkey, then we've been remotely activated
    if (pubKeyMasternode == activeMasternode.pubKeyMasternode && protocolVersion == PROTOCOL_VERSION) {
        activeMasternode.EnableHotColdMasterNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkIDString() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal)
        Relay(connman);

    return true;
}

void CMasternodeBroadcast::Relay(CConnman &connman) const
{
    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    connman.RelayInv(inv);
}

bool CMasternodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (!CMessageSigner::SignMessage(strMessage, sig, keyCollateralAddress, CPubKey::InputScriptType::SPENDP2PKH)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress.GetID(), sig, strMessage, errorMessage)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CMasternodePing::CMasternodePing()
{
    vin = CTxIn();
    blockHash.SetNull();
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


bool CMasternodePing::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!CMessageSigner::SignMessage(strMessage, vchSig, keyMasternode, CPubKey::InputScriptType::SPENDP2PKH)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!CMessageSigner::VerifyMessage(pubKeyMasternode.GetID(), vchSig, strMessage, errorMessage)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CMasternodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, CConnman &connman)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - New Ping - %s - %lli\n", blockHash.ToString(), sigTime);

    // see if we have this Masternode
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint(BCLog::MASTERNODE,"mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this masternode or
        // last ping was more then MASTERNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(MASTERNODE_MIN_MNP_SECONDS - 60, sigTime)) {
            std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

            std::string errorMessage = "";
            if (!CMessageSigner::VerifyMessage(pmn->pubKeyMasternode.GetID(), vchSig, strMessage, errorMessage)) {
                LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - Got bad Masternode address signature %s %s\n", vin.prevout.hash.ToString(), errorMessage);
                nDos = 33;
                return false;
            }

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - Masternode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Masternode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                LogPrint(BCLog::MASTERNODE,"CMasternodePing::CheckAndUpdate - Masternode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
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

            LogPrint(BCLog::MASTERNODE, "CMasternodePing::CheckAndUpdate - Masternode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay(connman);
            return true;
        }
        LogPrint(BCLog::MASTERNODE, "CMasternodePing::CheckAndUpdate - Masternode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint(BCLog::MASTERNODE, "CMasternodePing::CheckAndUpdate - Couldn't find compatible Masternode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CMasternodePing::Relay(CConnman &connman)
{
    CInv inv(MSG_MASTERNODE_PING, GetHash());
    connman.RelayInv(inv);
}
