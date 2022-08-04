#ifndef MOCK_UTXO_OWNERSHIP_DETECTOR_H
#define MOCK_UTXO_OWNERSHIP_DETECTOR_H
#include <gmock/gmock.h>
#include <I_UtxoOwnershipDetector.h>
class MockUtxoOwnershipDetector: public I_UtxoOwnershipDetector
{
public:
    MOCK_CONST_METHOD1(isMine, isminetype(const CTxOut& output));
    MOCK_CONST_METHOD1(isChange, bool(const CTxOut& output));
};
#endif// MOCK_UTXO_OWNERSHIP_DETECTOR_H