#ifndef BLOCK_REJECTS_H
#define BLOCK_REJECTS_H
#include <string>
#include <uint256.h>
struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};
#endif// BLOCK_REJECTS_H