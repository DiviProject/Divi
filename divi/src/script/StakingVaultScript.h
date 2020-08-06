#ifndef STAKING_VAULT_SCRIPT_H
#define STAKING_VAULT_SCRIPT_H
#include <vector>
#include <script/script.h>
#include <script/opcodes.h>
typedef std::vector<unsigned char> valtype;
CScript CreateStakingVaultScript(const valtype& ownerKeyHash, const valtype& vaultKeyHash);
CScript GetStakingVaultScriptTemplate();
#endif// STAKING_VAULT_SCRIPT_H