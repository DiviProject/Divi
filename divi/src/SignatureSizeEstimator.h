#ifndef SIGNATURE_SIZE_ESTIMATOR_H
#define SIGNATURE_SIZE_ESTIMATOR_H
class CKeyStore;
class CScript;
class SignatureSizeEstimator
{
public:
    static const unsigned MaximumScriptSigBytesForP2PKH;
    static unsigned MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey);
};
#endif// SIGNATURE_SIZE_ESTIMATOR_H