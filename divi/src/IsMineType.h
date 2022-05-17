#ifndef ISMINE_TYPE_H
#define ISMINE_TYPE_H
#include <stdint.h>
/** IsMine() return codes */
enum VaultType {
    NON_VAULT = 0,
    OWNED_VAULT = 1,
    MANAGED_VAULT = 2
};
enum class isminetype {
    ISMINE_NO = 0,
    //! Indicates that we dont know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_ONLY = 1,
    //! Indicates that we know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_MULTISIG = 1 << 1,
    ISMINE_SPENDABLE  = 1 << 2,
    ISMINE_OWNED_VAULT = 1 << 3,
    ISMINE_MANAGED_VAULT = 1 << 4,
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
    uint8_t underlyingBitMask() const
    {
        return bitmaskForOwnership_;
    }
};
#endif// ISMINE_TYPE_H