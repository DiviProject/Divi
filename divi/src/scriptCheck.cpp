//
// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scriptCheck.h>
#include <script/sigcache.h>
#include <Logging.h>
#include <primitives/transaction.h>
#include <coins.h>

CScriptCheck::CScriptCheck() : ptxTo(0), nIn(0), nFlags(0), error(SCRIPT_ERR_UNKNOWN_ERROR) {}

CScriptCheck::CScriptCheck(
    const CCoins& txFromIn,
    const CTransaction& txToIn,
    unsigned int nInIn,
    unsigned int nFlagsIn
    ) : scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey)
    , amountHeld(txFromIn.vout[txToIn.vin[nInIn].prevout.n].nValue)
    , ptxTo(&txToIn)
    , nIn(nInIn)
    , nFlags(nFlagsIn)
    , error(SCRIPT_ERR_UNKNOWN_ERROR)
{}

bool CScriptCheck::operator()()
{
    const CScript& scriptSig = ptxTo->vin[nIn].scriptSig;
    const CTxOut previousOutput(amountHeld,scriptPubKey);
    if (!VerifyScript(scriptSig, previousOutput, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn), &error)) {
        return ::error("CScriptCheck(): %s:%d VerifySignature failed: %s", ptxTo->ToStringShort(), nIn, ScriptErrorString(error));
    }
    return true;
}


void CScriptCheck::swap(CScriptCheck& check)
{
    std::swap(amountHeld,check.amountHeld);
    scriptPubKey.swap(check.scriptPubKey);
    std::swap(ptxTo, check.ptxTo);
    std::swap(nIn, check.nIn);
    std::swap(nFlags, check.nFlags);
    std::swap(error, check.error);
}

ScriptError CScriptCheck::GetScriptError() const{return error;}
