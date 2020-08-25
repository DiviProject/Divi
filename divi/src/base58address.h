#ifndef BASE58_ADDRESS_H
#define BASE58_ADDRESS_H
#include <base58data.h>
#include <destination.h>

class CKeyID;
class CScriptID;
class CChainParams;
class uint160;

/** base58-encoded DIVI addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CBitcoinAddress : public CBase58Data
{
public:
    bool Set(const CKeyID& id);
    bool Set(const CScriptID& id);
    bool Set(const CTxDestination& dest);
    bool IsValid() const;
    bool IsValid(const CChainParams& params) const;

    CBitcoinAddress();
    CBitcoinAddress(const CTxDestination& dest);
    CBitcoinAddress(const std::string& strAddress);
    CBitcoinAddress(const char* pszAddress);

    CTxDestination Get() const;
    bool GetKeyID(CKeyID& keyID) const;
    bool GetIndexKey(uint160& hashBytes, int& type) const;
    bool IsScript() const;
};

#endif// BASE58_ADDRESS_H