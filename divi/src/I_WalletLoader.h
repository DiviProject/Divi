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
    virtual void LoadWalletTransaction(const CWalletTx& wtxIn) = 0;
    virtual bool LoadWatchOnly(const CScript& dest) = 0;
    virtual bool LoadMinVersion(int nVersion) = 0;
    virtual bool LoadMultiSig(const CScript& dest) = 0;
    virtual bool LoadKey(const CKey& key, const CPubKey& pubkey) = 0;
    virtual bool LoadMasterKey(unsigned int masterKeyIndex, CMasterKey& masterKey) = 0;
    virtual bool LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret) = 0;
    virtual bool LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata, const bool updateFirstKeyTimestamp) = 0;
    virtual bool LoadDefaultKey(const CPubKey& vchPubKey, bool updateDatabase) = 0;
    virtual void LoadKeyPool(int nIndex, const CKeyPool &keypool) = 0;
    virtual bool LoadCScript(const CScript& redeemScript) = 0;
    virtual bool LoadHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool LoadCryptedHDChain(const CHDChain& chain, bool memonly) = 0;
    virtual bool LoadHDPubKey(const CHDPubKey &hdPubKey) = 0;
    virtual void ReserializeTransactions(const std::vector<uint256>& transactionIDs) = 0;
    virtual void LoadAddressLabel(const CTxDestination& address, const std::string newLabel) = 0;
};
#endif// I_WALLET_LOADER_H