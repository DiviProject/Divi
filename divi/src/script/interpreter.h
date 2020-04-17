// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_INTERPRETER_H
#define BITCOIN_SCRIPT_INTERPRETER_H

#include "script_error.h"

#include <vector>
#include <stdint.h>
#include <string>

class CScript;
class BaseSignatureChecker;

bool EvalScript(std::vector<std::vector<unsigned char> >& stack, const CScript& script, unsigned int flags, const BaseSignatureChecker& checker, ScriptError* error = NULL);
bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, unsigned int flags, const BaseSignatureChecker& checker, ScriptError* error = NULL);

#endif // BITCOIN_SCRIPT_INTERPRETER_H
