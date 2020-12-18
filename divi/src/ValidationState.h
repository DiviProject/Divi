// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATIONSTATE_H
#define BITCOIN_VALIDATIONSTATE_H

#include <string>

/** Abort with a message */
bool AbortNode(const std::string& msg, const std::string& userMessage = "");

/** Capture information about block/transaction validation */
class CValidationState
{
private:
    enum mode_state {
        MODE_VALID,   //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
        MODE_ERROR,   //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned char chRejectCode;
    bool corruptionPossible;

public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
    bool DoS(int level, bool ret = false, unsigned char chRejectCodeIn = 0, std::string strRejectReasonIn = "", bool corruptionIn = false);

    bool Invalid(bool ret = false,
        unsigned char _chRejectCode = 0,
        std::string _strRejectReason = "");

    bool Error(std::string strRejectReasonIn = "");

    bool Abort(const std::string& msg);

    bool IsValid() const;

    bool IsInvalid() const;

    bool IsError() const;

    bool IsInvalid(int& nDoSOut) const;

    bool CorruptionPossible() const;

    unsigned char GetRejectCode() const ;
    std::string GetRejectReason() const ;
};
#endif
