#ifndef I_BIP9_ACTIVATION_TRACKER_FACTORY_H
#define I_BIP9_ACTIVATION_TRACKER_FACTORY_H

class I_BIP9ActivationStateTracker;
struct BIP9Deployment;
struct ThresholdConditionCache;

class I_BIP9ActivationTrackerFactory
{
public:
    ~I_BIP9ActivationTrackerFactory(){}
    virtual I_BIP9ActivationStateTracker* create(
        const BIP9Deployment& bip,
        ThresholdConditionCache& thresholdCache) const = 0;
};

#endif //I_BIP9_ACTIVATION_TRACKER_FACTORY_H