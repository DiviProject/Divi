// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "interpreter.h"

#include <script/SignatureCheckers.h>

#include "script/scriptandsigflags.h"
#include "primitives/transaction.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "pubkey.h"
#include "script/script.h"
#include "uint256.h"

#include <script/StackManager.h>

using namespace std;

typedef vector<unsigned char> valtype;


bool CastToBool(const valtype& vch)
{
    for (unsigned int i = 0; i < vch.size(); i++)
    {
        if (vch[i] != 0)
        {
            // Can be negative zero
            if (i == vch.size()-1 && vch[i] == 0x80)
                return false;
            return true;
        }
    }
    return false;
}

/**
 * Script is a stack machine (like Forth) that evaluates a predicate
 * returning a bool indicating valid or not.  There are no loops.
 */
#define stacktop(i)  (stack.at(stack.size()+(i)))
#define altstacktop(i)  (altstack.at(altstack.size()+(i)))
static inline void popstack(vector<valtype>& stack)
{
    if (stack.empty())
        throw runtime_error("popstack() : stack empty");
    stack.pop_back();
}


bool static CheckMinimalPush(const valtype& data, opcodetype opcode) {
    if (data.size() == 0) {
        // Could have used OP_0.
        return opcode == OP_0;
    } else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) {
        // Could have used OP_1 .. OP_16.
        return opcode == OP_1 + (data[0] - 1);
    } else if (data.size() == 1 && data[0] == 0x81) {
        // Could have used OP_1NEGATE.
        return opcode == OP_1NEGATE;
    } else if (data.size() <= 75) {
        // Could have used a direct push (opcode indicating number of bytes pushed + those bytes).
        return opcode == data.size();
    } else if (data.size() <= 255) {
        // Could have used OP_PUSHDATA.
        return opcode == OP_PUSHDATA1;
    } else if (data.size() <= 65535) {
        // Could have used OP_PUSHDATA2.
        return opcode == OP_PUSHDATA2;
    }
    return true;
}

bool OpcodeIsDisabled(const opcodetype& opcode)
{
    if (opcode == OP_CAT ||
        opcode == OP_SUBSTR ||
        opcode == OP_LEFT ||
        opcode == OP_RIGHT ||
        opcode == OP_INVERT ||
        opcode == OP_AND ||
        opcode == OP_OR ||
        opcode == OP_XOR ||
        opcode == OP_2MUL ||
        opcode == OP_2DIV ||
        opcode == OP_MUL ||
        opcode == OP_DIV ||
        opcode == OP_MOD ||
        opcode == OP_LSHIFT ||
        opcode == OP_RSHIFT)
    {
        return true;
    }
    return false;
}



bool EvalScript(vector<vector<unsigned char> >& stack, const CScript& script, unsigned int flags, const BaseSignatureChecker& checker, ScriptError* serror)
{
    static const valtype vchFalse(0);
    static const valtype vchTrue(1, 1);

    CScript::const_iterator pc = script.begin();
    CScript::const_iterator pend = script.end();
    CScript::const_iterator pbegincodehash = script.begin();
    opcodetype opcode;
    valtype vchPushValue;

    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    if (script.size() > 10000)
    {    
        return set_error(serror, SCRIPT_ERR_SCRIPT_SIZE);
    }

    int nOpCount = 0;
    bool fRequireMinimal = (flags & SCRIPT_VERIFY_MINIMALDATA) != 0;

    StackOperationManager stackManager(stack,checker,flags);

    try
    {
        while (pc < pend)
        {
            //
            // Read instruction
            //
            if (!script.GetOp(pc, opcode, vchPushValue))
                return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
            if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
                return set_error(serror, SCRIPT_ERR_PUSH_SIZE);

            // Note how OP_RESERVED does not count towards the opcode limit.
            if (opcode > OP_16 && !stackManager.ReserveAdditionalOp() )
                return set_error(serror, SCRIPT_ERR_OP_COUNT);

            if (OpcodeIsDisabled(opcode))
                return set_error(serror, SCRIPT_ERR_DISABLED_OPCODE); // Disabled opcodes.

            if (!stackManager.ConditionalNeedsClosing() && 0 <= opcode && opcode <= OP_PUSHDATA4) {
                if (fRequireMinimal && !CheckMinimalPush(vchPushValue, opcode)) {
                    return set_error(serror, SCRIPT_ERR_MINIMALDATA);
                }
                stackManager.PushData(vchPushValue);
            }
            else if (!stackManager.ConditionalNeedsClosing() || (OP_IF <= opcode && opcode <= OP_ENDIF))
            {
                switch (opcode)
                {
                    case OP_NOP:
                    break;

                    case OP_NOP1: case OP_NOP2: case OP_NOP3: case OP_NOP4: case OP_NOP5:
                    case OP_NOP6: case OP_NOP7: case OP_NOP8: case OP_NOP9: case OP_NOP10:
                    case OP_1NEGATE: case OP_1: case OP_2: case OP_3: case OP_4: case OP_5: case OP_6: case OP_7: case OP_8:
                    case OP_9: case OP_10: case OP_11: case OP_12: case OP_13: case OP_14: case OP_15: case OP_16:
                    case OP_IF: case OP_NOTIF: case OP_ELSE: case OP_ENDIF:
                    case OP_TOALTSTACK: case OP_FROMALTSTACK: case OP_2DROP: case OP_2DUP: case OP_3DUP: case OP_2OVER: case OP_2ROT:
                    case OP_2SWAP: case OP_IFDUP: case OP_DEPTH: case OP_DROP: case OP_DUP: case OP_NIP: case OP_OVER: case OP_PICK: 
                    case OP_ROLL: case OP_ROT: case OP_SWAP: case OP_TUCK: case OP_SIZE:
                    case OP_VERIFY: case OP_EQUAL: case OP_EQUALVERIFY: case OP_META:
                    case OP_1ADD: case OP_1SUB: case OP_NEGATE: case OP_ABS: case OP_NOT: case OP_0NOTEQUAL:
                    case OP_ADD: case OP_SUB: case OP_BOOLAND: case OP_BOOLOR: case OP_NUMEQUAL: case OP_NUMEQUALVERIFY: case OP_NUMNOTEQUAL: 
                    case OP_LESSTHAN: case OP_GREATERTHAN: case OP_LESSTHANOREQUAL: case OP_GREATERTHANOREQUAL: case OP_MIN: case OP_MAX:
                    case OP_WITHIN: case OP_RIPEMD160: case OP_SHA1: case OP_SHA256: case OP_HASH160: case OP_HASH256:
                    {
                        if(!stackManager.GetOp(opcode)->operator()(opcode,serror)) return false;
                    }
                    break;                                   

                    case OP_CODESEPARATOR:
                    {
                        // Hash starts after the code separator
                        pbegincodehash = pc;
                    }
                    break;

                    case OP_CHECKSIG: case OP_CHECKSIGVERIFY: case OP_CHECKMULTISIG: case OP_CHECKMULTISIGVERIFY:
                    {
                        // (sig pubkey -- bool)
                        CScript scriptCode(pbegincodehash, pend);
                        if(!stackManager.GetOp(opcode)->operator()(opcode,scriptCode,serror)) return false;
                    }
                    break;

                    default:
                        return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
                }
            }

            // Size limits
            if (stackManager.TotalStackSize() > 1000)
                return set_error(serror, SCRIPT_ERR_STACK_SIZE);
        }
    }
    catch (...)
    {
        return set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    }

    if (!stackManager.ConditionalScopeIsBalanced())
        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);

    return set_success(serror);
}

bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, unsigned int flags, const BaseSignatureChecker& checker, ScriptError* serror)
{
    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

    if ((flags & SCRIPT_VERIFY_SIGPUSHONLY) != 0 && !scriptSig.IsPushOnly()) {
        return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
    }

    vector<vector<unsigned char> > stack, stackCopy;
    if (!EvalScript(stack, scriptSig, flags, checker, serror))
        // serror is set
        return false;
    if (flags & SCRIPT_VERIFY_P2SH)
        stackCopy = stack;
    if (!EvalScript(stack, scriptPubKey, flags, checker, serror))
        // serror is set
        return false;
    if (stack.empty())
        return set_error(serror, SCRIPT_ERR_EVAL_FALSE);

    if (CastToBool(stack.back()) == false)
        return set_error(serror, SCRIPT_ERR_EVAL_FALSE);

    // Additional validation for spend-to-script-hash transactions:
    if ((flags & SCRIPT_VERIFY_P2SH) && scriptPubKey.IsPayToScriptHash())
    {
        // scriptSig must be literals-only or validation fails
        if (!scriptSig.IsPushOnly())
            return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);

        // stackCopy cannot be empty here, because if it was the
        // P2SH  HASH <> EQUAL  scriptPubKey would be evaluated with
        // an empty stack and the EvalScript above would return false.
        assert(!stackCopy.empty());

        const valtype& pubKeySerialized = stackCopy.back();
        CScript pubKey2(pubKeySerialized.begin(), pubKeySerialized.end());
        popstack(stackCopy);

        if (!EvalScript(stackCopy, pubKey2, flags, checker, serror))
            // serror is set
            return false;
        if (stackCopy.empty())
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        if (!CastToBool(stackCopy.back()))
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        else
            return set_success(serror);
    }

    return set_success(serror);
}
