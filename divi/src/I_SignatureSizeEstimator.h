#ifndef I_SIGNATURE_SIZE_ESTIMATOR_H
#define I_SIGNATURE_SIZE_ESTIMATOR_H
class CKeyStore;
class CScript;
class I_SignatureSizeEstimator
{
public:
    virtual ~I_SignatureSizeEstimator(){}
    virtual unsigned MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey) const = 0;
    unsigned MaxBytesNeededForScriptSig(const CKeyStore& keystore,const CScript& scriptPubKey) const
    {
        return MaxBytesNeededForSigning(keystore,scriptPubKey) + 40u;
    };
};
#endif// I_SIGNATURE_SIZE_ESTIMATOR_H