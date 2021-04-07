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

class CMasternodeBroadcastFactory
{
public:
    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(
        const CMasternodeConfig::CMasternodeEntry configEntry,
        std::string& strErrorRet,
        CMasternodeBroadcast& mnbRet,
        bool fOffline = false,
        bool deferRelay = false);

    static bool Create(
        const CMasternodeConfig::CMasternodeEntry configEntry,
        CPubKey pubkeyCollateralAddress,
        std::string& strErrorRet,
        CMasternodeBroadcast& mnbRet,
        bool fOffline = false);

    static bool signPing(
        const CKey& keyMasternodeNew,
        const CPubKey& pubKeyMasternodeNew,
        CMasternodePing& mnp,
        std::string& strErrorRet);
private:
    static void createWithoutSignatures(
        const CTxIn& txin,
        const CService& service,
        const CPubKey& pubKeyCollateralAddressNew,
        const CPubKey& pubKeyMasternodeNew,
        MasternodeTier nMasternodeTier,
        bool deferRelay,
        CMasternodeBroadcast& mnbRet);

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

    static bool Create(const CTxIn& vin,
                       const CService& service,
                       const CKey& keyCollateralAddressNew,
                       const CPubKey& pubKeyCollateralAddressNew,
                       const CKey& keyMasternodeNew,
                       const CPubKey& pubKeyMasternodeNew,
                       MasternodeTier nMasternodeTier,
                       std::string& strErrorRet,
                       CMasternodeBroadcast& mnbRet,
                       bool deferRelay);
};
#endif //MASTERNODE_BROADCAST_FACTORY_H
