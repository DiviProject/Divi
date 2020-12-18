//
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockFileInfo.h>
#include <tinyformat.h>
#include <utiltime.h>

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}

void CBlockFileInfo::SetNull()
{
    nBlocks = 0;
    nSize = 0;
    nUndoSize = 0;
    nHeightFirst = 0;
    nHeightLast = 0;
    nTimeFirst = 0;
    nTimeLast = 0;
}

CBlockFileInfo::CBlockFileInfo()
{
    SetNull();
}

/** update statistics (does not update nSize) */
void CBlockFileInfo::AddBlock(unsigned int nHeightIn, uint64_t nTimeIn)
{
    if (nBlocks == 0 || nHeightFirst > nHeightIn)
        nHeightFirst = nHeightIn;
    if (nBlocks == 0 || nTimeFirst > nTimeIn)
        nTimeFirst = nTimeIn;
    nBlocks++;
    if (nHeightIn > nHeightLast)
        nHeightLast = nHeightIn;
    if (nTimeIn > nTimeLast)
        nTimeLast = nTimeIn;
}