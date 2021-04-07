#include <MasternodeBroadcastFactory.h>

#include <masternode.h>
#include <Logging.h>
#include <tinyformat.h>
#include <MasternodeHelpers.h>
#include <obfuscation.h>
#include <wallet.h>
#include <chain.h>
#include <base58address.h>
#include <TransactionDiskAccessor.h>
#include <timedata.h>
#include <WalletTx.h>
#include <script/standard.h>

extern CWallet* pwalletMain;
extern CChain chainActive;
extern bool fReindex;
extern bool fImporting;

static bool GetVinAndKeysFromOutput(const CWallet& wallet, COutput out, CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    txinRet = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!wallet.GetKey(keyID, keyRet)) {
        LogPrintf("CWallet::GetVinAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}

static bool GetMasternodeVinAndKeys(const CWallet& wallet, CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);

    int nOutputIndex;
    try {
        nOutputIndex = std::stoi(strOutputIndex.c_str());
    } catch (const std::exception& e) {
        LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
        return false;
    }

    if(auto walletTx = wallet.GetWalletTx(txHash))
    {
        if(wallet.IsSpent(*walletTx, nOutputIndex))
        {
            LogPrintf("%s -- Could not locate specified masternode vin, outpoint spent\n",__func__);
            return false;
        }

        return GetVinAndKeysFromOutput(wallet, COutput(walletTx, nOutputIndex, walletTx->GetNumberOfBlockConfirmations(), true), txinRet, pubKeyRet, keyRet);
    }
    else
    {
        LogPrintf("%s -- Could not locate any valid masternode vin\n",__func__);
        return false;
    }

    return false;
}

namespace
{

bool checkBlockchainSync(std::string& strErrorRet, bool fOffline)
{
     if (!fOffline && !IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Masternode";
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strErrorRet);
        return false;
    }
    return true;
}
bool setMasternodeKeys(
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
bool setMasternodeCollateralKeys(
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
    if (!GetMasternodeVinAndKeys(*pwalletMain,txin, masternodeCollateralKeyPair.second, masternodeCollateralKeyPair.first, txHash, outputIndex)) {
        strError = strprintf("Could not allocate txin %s:%s for masternode %s", txHash, outputIndex, service);
        LogPrint("masternode","CMasternodeBroadcastFactory::Create -- %s\n", strError);
        return false;
    }
    return true;
}

bool checkMasternodeCollateral(
    const CTxIn& txin,
    const std::string& txHash,
    const std::string& outputIndex,
    const std::string& service,
    MasternodeTier& nMasternodeTier,
    std::string& strErrorRet)
{
    nMasternodeTier = MasternodeTier::INVALID;
    uint256 blockHash;
    CTransaction fundingTx;
    if(GetTransaction(txin.prevout.hash,fundingTx,blockHash,true))
    {
        const CAmount collateralAmount = fundingTx.vout[txin.prevout.n].nValue;
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

bool createArgumentsFromConfig(
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

} // anonymous namespace

bool CMasternodeBroadcastFactory::Create(
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
    const int depthOfTx = ComputeMasternodeInputAge(mnb);
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

    mnbRet = CMasternodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMasternodeNew, nMasternodeTier, PROTOCOL_VERSION);
    const CMasternodePing mnp = (deferRelay
                                    ? createDelayedMasternodePing(mnbRet)
                                    : createCurrentPing(txin));
    mnbRet.lastPing = mnp;
    mnbRet.sigTime = mnp.sigTime;
}

bool CMasternodeBroadcastFactory::Create(
    const CTxIn& txin,
    const CService& service,
    const CKey& keyCollateralAddressNew,
    const CPubKey& pubKeyCollateralAddressNew,
    const CKey& keyMasternodeNew,
    const CPubKey& pubKeyMasternodeNew,
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
