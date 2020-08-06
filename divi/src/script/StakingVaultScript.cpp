#include <script/StakingVaultScript.h>

#include <vector>
#include <script/script.h>

typedef std::vector<unsigned char> valtype;

#define STAKING_VAULT(ownerKeyHash,vaultKeyHash) \
    CScript() << OP_IF << ownerKeyHash \
    << OP_ELSE << OP_NOP10 << vaultKeyHash << OP_ENDIF \
    <<  OP_OVER << OP_HASH160 << OP_EQUALVERIFY << OP_CHECKSIG

CScript CreateStakingVaultScript(const valtype& ownerKeyHash, const valtype& vaultKeyHash)
{
    return STAKING_VAULT(ownerKeyHash,vaultKeyHash);
}
CScript GetStakingVaultScriptTemplate()
{
    return STAKING_VAULT(OP_PUBKEYHASH,OP_PUBKEYHASH);
}
