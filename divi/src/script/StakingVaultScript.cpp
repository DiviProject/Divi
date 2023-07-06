#include <script/StakingVaultScript.h>

#include <vector>
#include <script/script.h>
#include <base58address.h>

typedef std::vector<unsigned char> valtype;

#define STAKING_VAULT(ownerKeyHash,vaultKeyHash) \
    CScript() << OP_IF << ownerKeyHash \
    << OP_ELSE << OP_REQUIRE_COINSTAKE << vaultKeyHash << OP_ENDIF \
    <<  OP_OVER << OP_HASH160 << OP_EQUALVERIFY << OP_CHECKSIG

CScript CreateStakingVaultScript(const valtype& ownerKeyHash, const valtype& vaultKeyHash)
{
    return STAKING_VAULT(ownerKeyHash,vaultKeyHash);
}
CScript GetStakingVaultScriptTemplate()
{
    return STAKING_VAULT(OP_PUBKEYHASH,OP_PUBKEYHASH);
}
bool IsStakingVaultScript(const CScript& scriptPubKey)
{
    static const CScript stakingVaultScriptTemplate = GetStakingVaultScriptTemplate();
    static const unsigned stakingVaultScriptSize = stakingVaultScriptTemplate.size()+2*20;
    if(scriptPubKey.size() != stakingVaultScriptSize) return false;
    CScript copyScript = scriptPubKey;
    bool keyPushSizeMatch =
        copyScript[1] == 0x14 &&
        copyScript[24] == 0x14;

    copyScript.erase(copyScript.begin()+24,copyScript.begin()+45);
    copyScript.insert(copyScript.begin()+24, OP_PUBKEYHASH);
    copyScript.erase(copyScript.begin()+1, copyScript.begin()+22);
    copyScript.insert(copyScript.begin()+1, OP_PUBKEYHASH);
    if(copyScript ==  stakingVaultScriptTemplate &&
        keyPushSizeMatch)
    {
        return true;
    }
    else
    {
        return false;
    }
}
bool GetStakingVaultPubkeyHashes(const CScript& scriptPubKey, std::pair<valtype,valtype>& pubkeyHashes)
{
    if(IsStakingVaultScript(scriptPubKey))
    {
        pubkeyHashes.first.resize(20);
        pubkeyHashes.first.assign(scriptPubKey.begin()+2,scriptPubKey.begin()+22);
        pubkeyHashes.second.resize(20);
        pubkeyHashes.second.assign(scriptPubKey.begin()+25,scriptPubKey.begin()+45);
        return true;
    }
    else
    {
        pubkeyHashes = std::pair<valtype,valtype>();
        return false;
    }
}

std::string GetVaultEncoding(const CScript& scriptPubKey)
{
    std::pair<valtype,valtype> pubkeyHashes;
    if(GetStakingVaultPubkeyHashes(scriptPubKey,pubkeyHashes))
    {
        CBitcoinAddress ownerAddress(CKeyID(uint160(pubkeyHashes.first)));
        CBitcoinAddress managerAddress(CKeyID( uint160(pubkeyHashes.second) ));
        return ownerAddress.ToString() + ":" + managerAddress.ToString();
    }
    return std::string("SCRIPT IS NOT A VAULT!");
}