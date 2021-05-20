#ifndef BLOCK_REJECTS_H
#define BLOCK_REJECTS_H
#include <string>
#include <uint256.h>
struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
    CBlockReject(unsigned char rejectCode, std::string reasonForRejection, uint256 blockHash)
    {
        chRejectCode = rejectCode;
        strRejectReason = reasonForRejection;
        hashBlock = blockHash;
    }
};
#endif// BLOCK_REJECTS_H