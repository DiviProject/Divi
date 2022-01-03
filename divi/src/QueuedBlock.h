#ifndef QUEUED_BLOCK_H
#define QUEUED_BLOCK_H
#include <uint256.h>
#include <stdint.h>
class CBlockIndex;
/** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
struct QueuedBlock {
    uint256 hash;
    const CBlockIndex* pindex;        //! Optional.
    int64_t nTime;              //! Time of "getdata" request in microseconds.
    int nValidatedQueuedBefore; //! Number of blocks queued with validated headers (globally) at the time this one is requested.
    bool fValidatedHeaders;     //! Whether this block has validated headers at the time of request.
};
#endif// QUEUED_BLOCK_H
