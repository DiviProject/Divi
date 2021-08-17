// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_ISMINE_H
#define BITCOIN_WALLET_ISMINE_H

#include <destination.h>

class CKeyStore;
class CScript;

/** IsMine() return codes */
enum class isminetype {
    ISMINE_NO = 0,
    //! Indicates that we dont know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_ONLY = 1,
    //! Indicates that we know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_MULTISIG = 2,
    ISMINE_SPENDABLE  = 4,
};
/** used for bitflags of isminetype */
class UtxoOwnershipFilter
{
private:
    uint8_t bitmaskForOwnership_;
public:
    UtxoOwnershipFilter(): bitmaskForOwnership_(0u){}
    UtxoOwnershipFilter(isminetype type): bitmaskForOwnership_(static_cast<uint8_t>(type)){}
    void addOwnershipType(const isminetype& type)
    {
        const uint8_t bitfieldDescriptionOfType = static_cast<uint8_t>(type);
        if((bitfieldDescriptionOfType & bitmaskForOwnership_) == 0u)
        {
            bitmaskForOwnership_ |= bitfieldDescriptionOfType;
        }
    }
    bool hasRequested(const isminetype& type) const
    {
        const uint8_t bitfieldDescriptionOfType = static_cast<uint8_t>(type);
        return (bitmaskForOwnership_ & bitfieldDescriptionOfType) > 0;
    }
};

enum VaultType {
    NON_VAULT = 0,
    OWNED_VAULT = 1,
    MANAGED_VAULT = 2
};

isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey);
isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey, VaultType& VaultType);
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest);

#endif // BITCOIN_WALLET_ISMINE_H
