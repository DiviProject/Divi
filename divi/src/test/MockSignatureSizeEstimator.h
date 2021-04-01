#ifndef MOCK_SIGNATURE_SIZE_ESTIMATOR_H
#define MOCK_SIGNATURE_SIZE_ESTIMATOR_H
#include <I_SignatureSizeEstimator.h>
#include <gmock/gmock.h>
class MockSignatureSizeEstimator: public I_SignatureSizeEstimator
{
public:
    MOCK_CONST_METHOD2(MaxBytesNeededForSigning, unsigned(const CKeyStore& keystore,const CScript& scriptPubKey));
};
#endif// MOCK_SIGNATURE_SIZE_ESTIMATOR_H