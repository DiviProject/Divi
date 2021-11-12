#include <MasternodeBroadcastFactory.h>

#include <masternode.h>
#include <Logging.h>
#include <tinyformat.h>
#include <MasternodeHelpers.h>
#include <obfuscation.h>
#include <chain.h>
#include <base58address.h>
#include <TransactionDiskAccessor.h>
#include <timedata.h>
#include <WalletTx.h>
#include <script/standard.h>
#include <keystore.h>

namespace
{

bool checkBlockchainSync(std::string& strErrorRet, bool fOffline)
{
     if (!fOffline && !IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrint("masternode","%s -- %s\n",__func__, strErrorRet);
        return false;
    }
    return true;
}
bool setMasternodeKeys(
    const CKeyStore& keyStore,
    std::pair<CKey,CPubKey>& masternodeKeyPair,
    std::string& strErrorRet)
{
    CKeyID keyID = masternodeKeyPair.second.GetID();
    if(!keyStore.GetKey(keyID,masternodeKeyPair.first))
    {
        strErrorRet = strprintf("Unknown masternode key %s", keyID.ToString());
        LogPrint("masternode","%s -- %s\n",__func__, strErrorRet);
        return false;
    }
    return true;
}

bool checkMasternodeCollateral(
    const std::string& service,
    const CAmount& collateralAmount,
    MasternodeTier& nMasternodeTier,
    std::string& strErrorRet)
{
    nMasternodeTier = CMasternode::GetTierByCollateralAmount(collateralAmount);
    if(!CMasternode::IsTierValid(nMasternodeTier))
    {
        strErrorRet = strprintf("Invalid tier selected for masternode %s, collateral value is: %d", service, collateralAmount);
        LogPrint("masternode","%s -- %s\n",__func__, strErrorRet);
        return false;
    }
    return true;
}

bool createArgumentsFromConfig(
    const CKeyStore& walletKeyStore,
    const CMasternodeConfig::CMasternodeEntry configEntry,
    std::string& strErrorRet,
    bool fOffline,
    bool collateralPrivKeyIsRemote,
    CTxIn& txin,
    std::pair<CKey,CPubKey>& masternodeKeyPair,
    MasternodeTier& nMasternodeTier)
{
    const std::string strService = configEntry.getIp();
    CTransaction fundingTx;
    uint256 blockHash;

    if (ReindexingOrImportingIsActive()) return false;

    if(!configEntry.parseInputReference(txin.prevout))
        return false;

    if(!GetTransaction(txin.prevout.hash,fundingTx,blockHash,true))
    {
        strErrorRet = strprintf("Could not find txin %s for masternode", txin.prevout.ToString());
        LogPrint("masternode","%s -- %s\n",__func__, strErrorRet);
        return false;
    }

    const CAmount& collateralAmount = fundingTx.vout[txin.prevout.n].nValue;
    //need correct blocks to send ping
    if (!checkBlockchainSync(strErrorRet,fOffline)||
        !setMasternodeKeys(walletKeyStore,masternodeKeyPair,strErrorRet) ||
        !checkMasternodeCollateral(strService, collateralAmount, nMasternodeTier, strErrorRet))
    {
        return false;
    }
    return true;
}

} // anonymous namespace

bool CMasternodeBroadcastFactory::CreateWithoutCollateralKey(
    const CKeyStore& walletKeyStore,
    const CMasternodeConfig::CMasternodeEntry configEntry,
    CPubKey pubkeyMasternode,
    CPubKey pubkeyCollateralAddress,
    std::string& strErrorRet,
    CMasternodeBroadcast& mnbRet)
{
    const bool fOffline = false;
    const bool collateralPrivateKeyIsRemote = true;
    const bool deferRelay = true;
    CTxIn txin;
    std::pair<CKey,CPubKey> masternodeKeyPair;
    MasternodeTier nMasternodeTier;

    masternodeKeyPair.second = pubkeyMasternode;

    if(!createArgumentsFromConfig(
        walletKeyStore,
        configEntry,
        strErrorRet,
        fOffline,
        collateralPrivateKeyIsRemote,
        txin,
        masternodeKeyPair,
        nMasternodeTier))
    {
        return false;
    }

    createWithoutSignatures(
        txin,
        CService(configEntry.getIp()),
        pubkeyCollateralAddress,
        pubkeyMasternode,
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

CMasternodeBroadcast CMasternodeBroadcastFactory::constructBroadcast(
    const CService& newAddr, const CTxIn& newVin,
    const CPubKey& pubKeyCollateralAddressNew, const CPubKey& pubKeyMasternodeNew,
    const MasternodeTier nMasternodeTier, const int protocolVersionIn)
{
    CMasternode mn;
    mn.vin = newVin;
    mn.addr = newAddr;
    mn.pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    mn.pubKeyMasternode = pubKeyMasternodeNew;
    mn.protocolVersion = protocolVersionIn;
    mn.nTier = nMasternodeTier;
    return {mn};
}

void CMasternodeBroadcastFactory::createWithoutSignatures(
    const CTxIn& txin,
    const CService& service,
    const CPubKey& pubKeyCollateralAddressNew,
    const CPubKey& pubKeyMasternodeNew,
    const MasternodeTier nMasternodeTier,
    bool deferRelay,
    CMasternodeBroadcast& mnbRet)
{
    LogPrint("masternode", "CMasternodeBroadcastFactory::createWithoutSignatures -- pubKeyCollateralAddressNew = %s, pubKeyMasternodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyMasternodeNew.GetID().ToString());

    mnbRet = CMasternodeBroadcastFactory::constructBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMasternodeNew, nMasternodeTier, PROTOCOL_VERSION);
    const CMasternodePing mnp = (deferRelay
                                    ? createDelayedMasternodePing(mnbRet)
                                    : createCurrentPing(txin));
    mnbRet.lastPing = mnp;

    /* We have to ensure that the sigtime of the broadcast itself is later
       than when the collateral gets its 15 confirmations (and it might not
       even be confirmed once yet).  */
    const auto* pindexConf = ComputeMasternodeConfirmationBlockIndex(mnbRet);
    if (pindexConf == nullptr) {
        /* The signature time is accepted on the network up to one hour
           into the future; we use 45 minutes here to ensure this is still
           fine even with some clock drift.  That will also be more than
           enough to be later than the confirmation block time assuming the
           collateral is going to be mined "now".  */
        mnbRet.sigTime = GetAdjustedTime() + 45 * 60;
        LogPrint("masternode", "Using future sigtime for masternode broadcast: %d\n", mnbRet.sigTime);
    } else {
        mnbRet.sigTime = pindexConf->GetBlockTime();
        LogPrint("masternode", "Using collateral confirmation time for broadcast: %d\n", mnbRet.sigTime);
    }
}