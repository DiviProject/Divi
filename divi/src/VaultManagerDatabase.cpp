#include <VaultManagerDatabase.h>

#include <utility>
#include <WalletTx.h>
#include <DataDirectory.h>
#include <sync.h>

#include <boost/scoped_ptr.hpp>
namespace
{

constexpr char DB_TX = 't';
constexpr char DB_SCRIPT = 's';

} // anonymous namespace

static std::pair<char,uint64_t> MakeTxIndex(uint64_t txIndex)
{
    return std::make_pair(DB_TX,txIndex);
}
static std::pair<char,uint64_t> MakeScriptIndex(uint64_t scriptIndex)
{
    return std::make_pair(DB_SCRIPT,scriptIndex);
}

VaultManagerDatabase::VaultManagerDatabase(
    std::string vaultID,
    size_t nCacheSize,
    bool fMemory,
    bool fWipe
    ):  CLevelDBWrapper(GetDataDir() / vaultID, nCacheSize, fMemory, fWipe)
    , txCount(0u)
    , scriptCount(0u)
    , updateCount_(0u)
{
}

bool VaultManagerDatabase::WriteTx(const CWalletTx& walletTransaction)
{
    if(Write(MakeTxIndex(txCount),walletTransaction))
    {
        ++updateCount_;
        ++txCount;
        return true;
    }
    return false;
}

bool VaultManagerDatabase::ReadTx(CWalletTx& walletTransaction)
{
    if(Read(MakeTxIndex(txCount),walletTransaction))
    {
        ++txCount;
        return true;
    }
    return false;
}

bool VaultManagerDatabase::WriteManagedScript(const CScript& managedScript)
{
    if(Write(MakeScriptIndex(scriptCount),managedScript))
    {
        ++updateCount_;
        ++scriptCount;
        return true;
    }
    return false;
}

bool VaultManagerDatabase::Sync()
{
    if(lastUpdateCount_ != updateCount_)
    {
        lastUpdateCount_ = updateCount_;
        return CLevelDBWrapper::Sync();
    }
    return true;
}

template<typename K> bool GetKey(leveldb::Slice slKey, K& key) {
    try {
        CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);
        ssKey >> key;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool VaultManagerDatabase::ReadManagedScripts(ManagedScripts& managedScripts)
{
    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());
    bool foundKey = false;
    std::pair<char,uint64_t> key;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    auto indexKey = MakeScriptIndex(0);
    ssKey.reserve(ssKey.GetSerializeSize(indexKey));
    ssKey << indexKey;

    leveldb::Slice slKey(&ssKey[0], ssKey.size());
    pcursor->Seek(slKey);

    while (pcursor->Valid() && !foundKey)
    {
        boost::this_thread::interruption_point();
        if (GetKey(pcursor->key(), key) && key.first == indexKey.first)
        {
            try
            {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
                CScript script;
                ssValue >> script;
                managedScripts.insert(script);
                scriptCount = std::max(scriptCount+1, key.second);
                pcursor->Next();
            }
            catch (const std::exception&)
            {
                return error("Failed to read managed scripts");
            }
        }
        else
        {
            break;
        }
    }
    return true;
}

bool VaultManagerDatabase::EraseManagedScript(const CScript& managedScript)
{
    bool foundKey = false;
    std::pair<char,uint64_t> key;
    {
        boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        auto indexKey = MakeScriptIndex(0);
        ssKey.reserve(ssKey.GetSerializeSize(indexKey));
        ssKey << indexKey;

        leveldb::Slice slKey(&ssKey[0], ssKey.size());
        pcursor->Seek(slKey);

        while (pcursor->Valid() && !foundKey)
        {
            boost::this_thread::interruption_point();
            if (GetKey(pcursor->key(), key) && key.first == indexKey.first)
            {
                try
                {
                    leveldb::Slice slValue = pcursor->value();
                    CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
                    CScript script;
                    ssValue >> script;
                    if(script ==managedScript)
                    {
                        ++updateCount_;
                        foundKey = true;
                        break;
                    }
                    pcursor->Next();
                }
                catch (const std::exception&)
                {
                    return error("Failed to access vault db to erase managed scripts");
                }
            }
            else
            {
                break;
            }
        }
    }
    return foundKey? Erase(key): error("Failed to find managed script to erase");
}
