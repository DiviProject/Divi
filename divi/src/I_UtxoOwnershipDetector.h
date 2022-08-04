#ifndef I_UTXO_OWNERSHIP_DETECTOR_H
#define I_UTXO_OWNERSHIP_DETECTOR_H
#include <IsMineType.h>
class CTxOut;
class I_UtxoOwnershipDetector
{
public:
    virtual ~I_UtxoOwnershipDetector(){}
    virtual isminetype isMine(const CTxOut& output) const = 0;
    virtual bool isChange(const CTxOut& output) const = 0;
};
#endif// I_UTXO_OWNERSHIP_DETECTOR_H