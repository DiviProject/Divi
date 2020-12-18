#ifndef BIP9_DEPLOYMENT_H
#define BIP9_DEPLOYMENT_H
#include <cstdint>
#include <limits>
#include <string>

/** What block version to use for new blocks (pre versionbits) */
static const int32_t VERSIONBITS_LAST_OLD_BLOCK_VERSION = 4;
/** What bits to set in version for versionbits blocks */
static const int32_t VERSIONBITS_TOP_BITS = 0x20000000UL;
/** What bitmask determines whether versionbits is in use */
static const int32_t VERSIONBITS_TOP_MASK = 0xE0000000UL;
/** Total bits available for versionbits */
static const int32_t VERSIONBITS_NUM_BITS = 29;

/** BIP 9 defines a finite-state-machine to deploy a softfork in multiple stages.
 *  State transitions happen during retarget period if conditions are met
 *  In case of reorg, transitions can go backward. Without transition, state is
 *  inherited between periods. All blocks of a period share the same state.
 */
enum class ThresholdState {
    DEFINED,   // First state that each softfork starts out as. The genesis block is by definition in this state for each deployment.
    STARTED,   // For blocks past the starttime.
    LOCKED_IN, // For one retarget period after the first retarget period with STARTED blocks of which at least threshold have the associated bit set in nVersion.
    ACTIVE,    // For all blocks after the LOCKED_IN retarget period (final state)
    FAILED,    // For all blocks once the first retarget period after the timeout time is hit, if LOCKED_IN wasn't already reached (final state)
};

struct BIP9Deployment 
{
    /** Name of deployment for later caching*/
    const std::string deploymentName;
    /** Bit position to select the particular bit in nVersion. */
    const int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    const int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    const int64_t nTimeout;
    // Number of blocks to look back & number of blocks needed
    const int nPeriod;
    const int threshold;

    mutable ThresholdState state = ThresholdState::DEFINED;

    BIP9Deployment();

    BIP9Deployment(
        std::string name,
        unsigned bitIndex, 
        int64_t startTime, 
        int64_t timeout,
        int blockPeriod,
        int blockThreshold
        );

    BIP9Deployment& operator=(const BIP9Deployment& other);

    void setState(ThresholdState updatedState) const;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
    static constexpr int MAX_VERSION_BITS_DEPLOYMENTS = 1;
};
#endif //BIP9_DEPLOYMENT_H