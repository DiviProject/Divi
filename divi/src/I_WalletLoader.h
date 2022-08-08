#ifndef I_WALLET_LOADER_H
#define I_WALLET_LOADER_H
#include <vector>
#include <uint256.h>
#include <destination.h>
#include <string>
class CWalletTx;
class CScript;
class CKey;
class CPubKey;
class CMasterKey;
class CKeyPool;
class CHDChain;
class CHDPubKey;
class CKeyMetadata;

class I_WalletLoader
{
public:
    virtual void loadWalletTransaction(const CWalletTx& wtxIn) = 0;
    virtual bool loadWatchOnly(const CScript& dest) = 0;
    virtual bool loadMinVersion(int nVersion) = 0;
    virtual bool loadMultiSig(const CScript& dest) = 0;
    virtual bool loadKey(const CKey& key, const CPubKey& pubkey) = 0;
    virtual bool loadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey) = 0;
    virtual bool loadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret) = 0;
    virtual bool loadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata, const bool updateFirstKeyTimestamp) = 0;
    virtual bool loadDefaultKey(const CPubKey& vchPubKey, bool updateDatabase) = 0;
    virtual void loadKeyPool(int nIndex, const CKeyPool &keypool) = 0;
    virtual bool loadCScript(const CScript& redeemScript) = 0;
    virtual bool loadHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool loadCryptedHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool loadHDPubKey(const CHDPubKey &hdPubKey) = 0;
    virtual void reserializeTransactions(const std::vector<uint256>& transactionIDs) = 0;
    virtual void loadAddressLabel(const CTxDestination& address, const std::string newLabel) = 0;
};
#endif// I_WALLET_LOADER_H