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
        CTxIn txin,
        CService service,
        CPubKey pubKeyCollateralAddressNew,
        CPubKey pubKeyMasternodeNew,
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
    static bool checkBlockchainSync(
        std::string& strErrorRet, bool fOffline);
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
#endif //MASTERNODE_BROADCAST_FACTORY_H