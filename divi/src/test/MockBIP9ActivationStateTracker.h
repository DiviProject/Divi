#ifndef MOCK_BIP9_ACTIVATION_STATE_TRACKER_H
#define MOCK_BIP9_ACTIVATION_STATE_TRACKER_H

#include <I_BIP9ActivationStateTracker.h>
#include <gmock/gmock.h>

class MockBIP9ActivationStateTracker: public I_BIP9ActivationStateTracker
{
public:
    MOCK_CONST_METHOD1(bipIsSignaledFor, bool(const CBlockIndex* shallowBlockIndex));
    MOCK_METHOD1(update, bool(const CBlockIndex* shallowBlockIndex) );
    MOCK_CONST_METHOD1(getLastCachedStatePriorToBlockIndex, ThresholdState(const CBlockIndex* shallowBlockIndex));
};

#endif //MOCK_BIP9_ACTIVATION_STATE_TRACKER_H