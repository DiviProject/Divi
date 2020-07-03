#ifndef BIP9_ACTIVATION_MANAGER_H
#define BIP9_ACTIVATION_MANAGER_H

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

class I_BIP9ActivationStateTracker;
struct ThresholdConditionCache;
struct BIP9Deployment;
class CBlockIndex;
class I_BIP9ActivationTrackerFactory;

class BIP9ActivationManager
{
private:
    static constexpr int MAXIMUM_SIMULTANEOUS_DEPLOYMENTS = 29;
    std::vector<std::shared_ptr<ThresholdConditionCache>> thresholdCaches_;
    std::vector<std::shared_ptr<I_BIP9ActivationStateTracker>> bip9ActivationTrackers_;

    std::vector<std::shared_ptr<BIP9Deployment>> knownBIPs_;
    uint32_t bitfieldOfBipsInUse_;

    std::unordered_map<std::string, unsigned> bipIndexByName_;
    I_BIP9ActivationTrackerFactory& trackerFactory_;
public:
    enum BIPStatus
    {
        UNKNOWN_BIP,
        IN_PROGRESS
    };

    BIP9ActivationManager(I_BIP9ActivationTrackerFactory& factory);
    bool networkEnabledBIP(std::string bipName,const CBlockIndex* chainTip = NULL) const;
    BIPStatus getBIPStatus(std::string bipName) const;
    void addBIP(const BIP9Deployment& bip);
};

#endif// BIP9_ACTIVATION_MANAGER_H