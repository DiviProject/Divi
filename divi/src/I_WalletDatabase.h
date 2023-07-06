#ifndef I_WALLET_DATABASE_H
#define I_WALLET_DATABASE_H
#include <string>
#include <vector>
#include <PrivKey.h>

class CAccount;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWalletTx;
class uint160;
class uint256;
class BlockMap;
class CChain;
class I_WalletLoader;
class CHDChain;
class CHDPubKey;
class CKeyMetadata;
class CPubKey;

using WalletTxVector = std::vector<CWalletTx>;

/** Error statuses for the wallet database */
enum DBErrors {
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE,
    DB_REWRITTEN,
    DB_LOAD_OK_FIRST_RUN,
    DB_LOAD_OK_RELOAD,
};

/** Access to the wallet database (wallet.dat) */
class I_WalletDatabase
{
public:
    virtual ~I_WalletDatabase(){};

    virtual bool AtomicWriteBegin() = 0;
    virtual bool AtomicWriteEnd(bool commitChanges) = 0;

    virtual bool WriteName(const std::string& strAddress, const std::string& strName) = 0;
    virtual bool EraseName(const std::string& strAddress) = 0;
    virtual bool WriteTx(uint256 hash, const CWalletTx& wtx) = 0;
    virtual bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta) = 0;
    virtual bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata& keyMeta) = 0;
    virtual bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey) = 0;
    virtual bool WriteCScript(const uint160& hash, const CScript& redeemScript) = 0;
    virtual bool EraseCScript(const uint160& hash) = 0;
    virtual bool WriteWatchOnly(const CScript& script) = 0;
    virtual bool EraseWatchOnly(const CScript& script) = 0;
    virtual bool WriteMultiSig(const CScript& script) = 0;
    virtual bool EraseMultiSig(const CScript& script) = 0;
    virtual bool WriteBestBlock(const CBlockLocator& locator) = 0;
    virtual bool ReadBestBlock(CBlockLocator& locator) = 0;
    virtual bool WriteDefaultKey(const CPubKey& vchPubKey) = 0;
    virtual bool ReadPool(int64_t nPool, CKeyPool& keypool) = 0;
    virtual bool WritePool(int64_t nPool, const CKeyPool& keypool) = 0;
    virtual bool ErasePool(int64_t nPool) = 0;
    virtual bool WriteMinVersion(int nVersion) = 0;
    virtual bool WriteHDChain(const CHDChain& chain) = 0;
    virtual bool WriteCryptedHDChain(const CHDChain& chain) = 0;
    virtual bool WriteHDPubKey(const CHDPubKey& hdPubKey, const CKeyMetadata& keyMeta) = 0;
    virtual DBErrors LoadWallet(I_WalletLoader& pwallet) = 0;
    virtual bool RewriteWallet() = 0;
};
#endif // I_WALLET_DATABASE_H