#ifndef DIVI_CONSENSUS_CONSENSUS_H
#define DIVI_CONSENSUS_CONSENSUS_H

#include <stdint.h>
#include <map>
#include <string>

#include "uint256.h"

namespace Consensus
{
    class Consensus
    {
        public:
            /** The maximum allowed size for a serialized block, in bytes (only for buffer size limits) */
            const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000;

            /** The maximum allowed cost for a block, see BIP 141 (network rule) */
            const unsigned int MAX_BLOCK_COST = 4000000;

            /** The maximum allowed size for a block excluding witness data, in bytes (network rule) */
            const unsigned int MAX_BLOCK_BASE_SIZE = 1000000;

            /** The maximum allowed number of signature check operations in a block (network rule) */
            const int64_t MAX_BLOCK_SIGOPS_COST = 80000;

            /** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
            const int COINBASE_MATURITY = 100;

            /** Flags for nSequence and nLockTime locks */
            enum
            {
                /* Interpret sequence numbers as relative lock-time constraints. */
                LOCKTIME_VERIFY_SEQUENCE = (1 << 0),

                /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
                LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
            };

            enum DeploymentPos
            {
                DEPLOYMENT_TESTDUMMY,
                DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
                DEPLOYMENT_SEGWIT, // Deployment of BIP141 and BIP143

                // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
                MAX_VERSION_BITS_DEPLOYMENTS
            };

            /**
             * Struct for each individual consensus rule change using BIP9.
             */
            struct BIP9Deployment
            {
                /** Bit position to select the particular bit in nVersion. */
                int bit;
                /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
                int64_t nStartTime;
                /** Timeout/expiry MedianTime for the deployment attempt. */
                int64_t nTimeout;
            };

            /**
             * Parameters that influence chain consensus.
             */
            struct Params
            {
                uint256 hashGenesisBlock;
                int nSubsidyHalvingInterval;

                /** Used to check majorities for block version upgrade */
                int nMajorityEnforceBlockUpgrade;
                int nMajorityRejectBlockOutdated;
                int nMajorityWindow;

                /** Block height and hash at which BIP34 becomes active */
                int BIP34Height;
                uint256 BIP34Hash;

                /**
                 * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargetting period,
                 * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
                 * Examples: 1916 for 95%, 1512 for testchains.
                 */
                uint32_t nRuleChangeActivationThreshold;
                uint32_t nMinerConfirmationWindow;
                BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];

                /** Proof of work parameters */
                uint256 powLimit;
                bool fPowAllowMinDifficultyBlocks;
                bool fPowNoRetargeting;
                int64_t nPowTargetSpacing;
                int64_t nPowTargetTimespan;
                int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
            };
    }
}

#endif
