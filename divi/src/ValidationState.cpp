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



