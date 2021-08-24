#include <VaultManagerDatabase.h>

#include <utility>
#include <WalletTx.h>
#include <DataDirectory.h>
#include <sync.h>

static CCriticalSection vaultIdLock;
static unsigned vaultId;

static std::string getNewVaultId()
{
    LOCK(vaultIdLock);
    return std::string("vault") + std::to_string(vaultId++);
}


static std::pair<std::string,uint64_t> MakeTxIndex(uint64_t txIndex)
{
    return std::make_pair("tx",txIndex);
}
static std::pair<std::string,uint64_t> MakeScriptIndex(uint64_t scriptIndex)
{
    return std::make_pair("script",scriptIndex);
}

VaultManagerDatabase::VaultManagerDatabase(
    size_t nCacheSize,
    bool fMemory,
    bool fWipe
    ):  CLevelDBWrapper(GetDataDir() / getNewVaultId(), nCacheSize, fMemory, fWipe)
    , txIndex(0u)
    , scriptIDLookup()
{
}

bool VaultManagerDatabase::WriteTx(const CWalletTx& walletTransaction)
{
    const bool success = Write(MakeTxIndex(txIndex),walletTransaction);
    if(success) ++txIndex;
    return success;
}

bool VaultManagerDatabase::ReadTx(CWalletTx& walletTransaction)
{
    const bool success = Read(MakeTxIndex(txIndex),walletTransaction);
    if(success) ++txIndex;
    return success;
}

bool VaultManagerDatabase::WriteManagedScript(const CScript& managedScript)
{
    const CScriptID id(managedScript);
    if(scriptIDLookup.count(id) == 0u)
    {
        uint64_t nextIndex = scriptIDLookup.size();
        if(Write(MakeScriptIndex(nextIndex),managedScript))
        {
            scriptIDLookup[id] =  nextIndex;
            return true;
        }
        return false;
    }
    return false;
}

bool VaultManagerDatabase::EraseManagedScript(const CScript& managedScript)
{
    const CScriptID id(managedScript);
    if(scriptIDLookup.count(id) > 0)
    {
        if(Erase(MakeScriptIndex(scriptIDLookup[id])))
        {
            scriptIDLookup.erase(id);
            return true;
        }
        return false;
    }
    return false;
}

bool VaultManagerDatabase::ReadManagedScripts(ManagedScripts& managedScripts)
{
    uint64_t dummyScriptIndex = 0u;
    CScript managedScript;
    while(Read(MakeScriptIndex(dummyScriptIndex),managedScript))
    {
        CScriptID id(managedScript);
        scriptIDLookup[id] = dummyScriptIndex;

        managedScripts.insert(managedScript);
        managedScript.clear();
        ++dummyScriptIndex;
    }
    return true;
}