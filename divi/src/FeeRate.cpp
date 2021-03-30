// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "FeeRate.h"

#include "tinyformat.h"

//Fee = TV * TVM *  TS / TSM

const unsigned CFeeRate::FEE_INCREMENT_STEPSIZE = 1000u;
CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nSize)
{
    if (nSize > 0)
        nSatoshisPerK = nFeePaid * FEE_INCREMENT_STEPSIZE / nSize;
    else
        nSatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nSize) const
{
    CAmount nFee = nSatoshisPerK * nSize / FEE_INCREMENT_STEPSIZE;

    if (nFee == 0 && nSatoshisPerK > 0)
        nFee = nSatoshisPerK;

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d DIV/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN);
}
