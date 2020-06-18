#ifndef BIP9_ACTIVATION_FEATURE_CONTAINER_H
#define BIP9_ACTIVATION_FEATURE_CONTAINER_H

#include <memory>
class I_BIP9ActivationTrackerFactory;
class BIP9ActivationManager;

class BIP9ActivationFeatureContainer
{
private:
    std::shared_ptr<I_BIP9ActivationTrackerFactory> activationStateTrackerFactory_;
    std::shared_ptr<BIP9ActivationManager> activationManager_;
public:
    BIP9ActivationFeatureContainer();

    BIP9ActivationManager& activationManager();
};

#endif // BIP9_ACTIVATION_FEATURE_CONTAINER_H