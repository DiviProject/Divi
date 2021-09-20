#ifndef MASTERNODE_BROADCAST_FACTORY_H
#define MASTERNODE_BROADCAST_FACTORY_H
#include <utility>
#include <string>
#include <pubkey.h>
#include <key.h>
#include <masternodeconfig.h>
#include <netbase.h>
#include <primitives/transaction.h>
#include <masternode-tier.h>

class CMasternodeBroadcast;
class CMasternodePing;
class CKeyStore;

class CMasternodeBroadcastFactory
{
public:
    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool CreateWithoutCollateralKey(
        const CKeyStore& walletKeyStore,
        const CMasternodeConfig::CMasternodeEntry configEntry,
        CPubKey pubkeyMasternode,
        CPubKey pubkeyCollateralAddress,
        std::string& strErrorRet,
        CMasternodeBroadcast& mnbRet);

    static bool signPing(
        const CKey& keyMasternodeNew,
        const CPubKey& pubKeyMasternodeNew,
        CMasternodePing& mnp,
        std::string& strErrorRet);

    static bool signBroadcast(
        CKey keyCollateralAddressNew,
        CMasternodeBroadcast& mnb,
        std::string& strErrorRet);
private:
    static CMasternodeBroadcast constructBroadcast(
        const CService& newAddr,
        const CTxIn& newVin,
        const CPubKey& pubKeyCollateralAddress,
        const CPubKey& pubKeyMasternode,
        MasternodeTier nMasternodeTier,
        int protocolVersionIn);
    static void createWithoutSignatures(
        const CTxIn& txin,
        const CService& service,
        const CPubKey& pubKeyCollateralAddressNew,
        const CPubKey& pubKeyMasternodeNew,
        MasternodeTier nMasternodeTier,
        bool deferRelay,
        CMasternodeBroadcast& mnbRet);
};
#endif //MASTERNODE_BROADCAST_FACTORY_H
