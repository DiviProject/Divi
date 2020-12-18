#ifndef MOCK_BIP9_ACTIVATION_TRACKER_FACTORY_H
#define MOCK_BIP9_ACTIVATION_TRACKER_FACTORY_H
#include <gmock/gmock.h>
#include <I_BIP9ActivationTrackerFactory.h>

class MockBIP9ActivationTrackerFactory: public I_BIP9ActivationTrackerFactory
{
public:
    MOCK_CONST_METHOD2(create, I_BIP9ActivationStateTracker*(const BIP9Deployment& bip, ThresholdConditionCache& thresholdCache));
};

#endif //MOCK_BIP9_ACTIVATION_TRACKER_FACTORY_H