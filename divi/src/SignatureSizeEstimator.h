#ifndef SIGNATURE_SIZE_ESTIMATOR_H
#define SIGNATURE_SIZE_ESTIMATOR_H
#include <I_SignatureSizeEstimator.h>
class CKeyStore;
class CScript;
class SignatureSizeEstimator: public I_SignatureSizeEstimator
{
private:
    static unsigned MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey,bool isSubscript);
public:
    static const unsigned MaximumScriptSigBytesForP2PK;
    static const unsigned MaximumScriptSigBytesForP2PKH;
    virtual unsigned MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey) const;
};
#endif// SIGNATURE_SIZE_ESTIMATOR_H