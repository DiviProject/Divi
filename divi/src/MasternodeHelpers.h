#ifndef MASTERNODE_HELPERS_H
#define MASTERNODE_HELPERS_H
#include <stdint.h>
class uint256;
class CBlockIndex;
class CMasternode;
class CMasternodePing;
class CTxIn;
class COutPoint;

bool CollateralIsExpectedAmount(const COutPoint &outpoint, int64_t expectedAmount);
bool IsBlockchainSynced();

/** Returns the block hash that is used in the masternode scoring / ranking
 *  logic for the winner of block nBlockHeight.  (It is the hash of the
 *  block nBlockHeight-101, but that's an implementation detail.)  */
bool GetBlockHashForScoring(uint256& hash, int nBlockHeight);

/** Returns the scoring hash corresponding to the given CBlockIndex
 *  offset by N.  In other words, that is used to compute the winner
 *  that should be payed in block pindex->nHeight+N.
 *
 *  In contrast to GetBlockHashForScoring, this works entirely independent
 *  of chainActive, and is guaranteed to look into the correct ancestor
 *  chain independent of potential reorgs.  */
bool GetBlockHashForScoring(uint256& hash,
                            const CBlockIndex* pindex, const int offset);

const CBlockIndex* ComputeCollateralBlockIndex(const CMasternode& masternode);
const CBlockIndex* ComputeMasternodeConfirmationBlockIndex(const CMasternode& masternode);
int ComputeMasternodeInputAge(const CMasternode& masternode);
CMasternodePing createCurrentPing(const CTxIn& newVin);
#endif // MASTERNODE_HELPERS_H