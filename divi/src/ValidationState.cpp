// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATIONSTATE_H
#include <ValidationState.h>
#endif

bool CValidationState::DoS(int level,
                           bool ret ,
                           unsigned char chRejectCodeIn ,
                           std::string strRejectReasonIn ,
                           bool corruptionIn )
{
    chRejectCode = chRejectCodeIn;
    strRejectReason = strRejectReasonIn;
    corruptionPossible = corruptionIn;
    if (mode == MODE_ERROR)
        return ret;
    nDoS += level;
    mode = MODE_INVALID;
    return ret;
}


bool CValidationState::Invalid(bool ret ,
                               unsigned char _chRejectCode ,
                               std::string _strRejectReason)
{
    return DoS(0, ret, _chRejectCode, _strRejectReason);
}

bool CValidationState::Error(std::string strRejectReasonIn)
{
    if (mode == MODE_VALID)
        strRejectReason = strRejectReasonIn;
    mode = MODE_ERROR;
    return false;
}

bool CValidationState::Abort(const std::string& msg)
{
    AbortNode(msg);
    return Error(msg);
}

bool CValidationState::IsValid() const
{
    return mode == MODE_VALID;
}

bool CValidationState::IsInvalid() const
{
    return mode == MODE_INVALID;
}

bool CValidationState::IsError() const
{
    return mode == MODE_ERROR;
}

bool CValidationState::IsInvalid(int& nDoSOut) const
{
    if (IsInvalid()) {
        nDoSOut = nDoS;
        return true;
    }
    return false;
}

bool CValidationState::CorruptionPossible() const
{
    return corruptionPossible;
}

unsigned char CValidationState::GetRejectCode() const
{
    return chRejectCode;
}

std::string CValidationState::GetRejectReason() const
{
    return strRejectReason;
}



