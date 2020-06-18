#include <BIP9ActivationFeatureContainer.h>

#include <I_BIP9ActivationTrackerFactory.h>
#include <BIP9ActivationManager.h>
#include <CachedBIP9ActivationStateTracker.h>

class ActivationTrackerFactory: public I_BIP9ActivationTrackerFactory
{
public:
    virtual ~ActivationTrackerFactory(){}
    virtual I_BIP9ActivationStateTracker* create(const BIP9Deployment& bip, ThresholdConditionCache& cache) const override
    {
        return new CachedBIP9ActivationStateTracker(bip,cache);
    }
};


BIP9ActivationFeatureContainer::BIP9ActivationFeatureContainer(
    ): activationStateTrackerFactory_(new ActivationTrackerFactory)
    , activationManager_(std::make_shared<BIP9ActivationManager>(*activationStateTrackerFactory_))
{

}  
BIP9ActivationManager& BIP9ActivationFeatureContainer::activationManager()
{
    return *activationManager_;
}