#include <BIP9ActivationManager.h>

#include <BIP9Deployment.h>
#include <algorithm>
#include <CachedBIP9ActivationStateTracker.h>
#include <ThresholdConditionCache.h>
#include <I_BIP9ActivationTrackerFactory.h>

BIP9ActivationManager::BIP9ActivationManager(
    I_BIP9ActivationTrackerFactory& factory
    ): thresholdCaches_()
    , bip9ActivationTrackers_()
    , knownBIPs_()
    , bitfieldOfBipsInUse_(0u)
    , bipIndexByName_()
    , trackerFactory_(factory)
{
    knownBIPs_.reserve(BIP9ActivationManager::MAXIMUM_SIMULTANEOUS_DEPLOYMENTS);
}

bool BIP9ActivationManager::networkEnabledBIP(std::string bipName, const CBlockIndex* chainTip) const
{
    auto it = bipIndexByName_.find(bipName);
    if(it == bipIndexByName_.end()) return false;

    auto& tracker = bip9ActivationTrackers_[it->second];
    tracker->update(chainTip);
    auto result = tracker->getLastCachedStatePriorToBlockIndex(chainTip);
    return result == ThresholdState::ACTIVE;
}

BIP9ActivationManager::BIPStatus BIP9ActivationManager::getBIPStatus(std::string bipName) const
{
    auto it = std::find_if(knownBIPs_.begin(), knownBIPs_.end(), [&bipName](const std::shared_ptr<BIP9Deployment>& bip) { return bip->deploymentName == bipName;} );
    return (it==knownBIPs_.end())?BIP9ActivationManager::UNKNOWN_BIP : BIP9ActivationManager::IN_PROGRESS;
}

void BIP9ActivationManager::addBIP(const BIP9Deployment& bip)
{
    if(bip.bit >= MAXIMUM_SIMULTANEOUS_DEPLOYMENTS) return;
    
    uint32_t bipMask = ((uint32_t)1 << bip.bit);
    if( (bipMask & bitfieldOfBipsInUse_) == 0)
    {
        bipIndexByName_.insert({bip.deploymentName, knownBIPs_.size()});
        knownBIPs_.push_back(std::make_shared<BIP9Deployment>(bip));
        thresholdCaches_.push_back(std::make_shared<ThresholdConditionCache>());

        I_BIP9ActivationStateTracker* tracker = 
            trackerFactory_.create(
                *knownBIPs_.back(), 
                *thresholdCaches_.back());

        if(!tracker)
        {
            bipIndexByName_.erase(bip.deploymentName);
            knownBIPs_.pop_back();
            thresholdCaches_.pop_back();
            return;
        }

        bitfieldOfBipsInUse_ |= bipMask;

        bip9ActivationTrackers_.emplace_back( tracker );
    }
}