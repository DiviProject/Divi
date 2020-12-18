#include <script/StackManager.h>

#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"

#include <script/opcodes.h>
#include <vector>
#include <script/scriptandsigflags.h>
#include <algorithm>
#include <memory>

#include <script/SignatureCheckers.h>
#include <serialize.h>
#include <hash.h>

#define MAXIMUM_NUMBER_OF_OPCODES 201

namespace Helpers
{
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

    inline bool set_error(ScriptError* ret, const ScriptError serror)
    {
        if (ret)
            *ret = serror;
        return false;
    }
}


void ConditionalScopeStackManager::CheckConditionalScopeStatus()
{
    conditionalScopeNeedsClosing = std::count(conditionalScopeReadStatusStack.begin(),conditionalScopeReadStatusStack.end(),false)!=0;
}

bool ConditionalScopeStackManager::StackIsEmpty() const
{
    return conditionalScopeReadStatusStack.empty();
}
bool ConditionalScopeStackManager::ConditionalScopeNeedsClosing() const
{
    return conditionalScopeNeedsClosing;
}

void ConditionalScopeStackManager::OpenScope(bool conditionStatus)
{
    conditionalScopeReadStatusStack.push_back(conditionStatus);
    conditionalScopeNeedsClosing |= (conditionStatus==false);
}
bool ConditionalScopeStackManager::TryElse()
{
    if(conditionalScopeReadStatusStack.empty()) return false;
    conditionalScopeReadStatusStack.back()= !conditionalScopeReadStatusStack.back();
    CheckConditionalScopeStatus();
    return true;
}
bool ConditionalScopeStackManager::TryCLoseScope()
{
    if(conditionalScopeReadStatusStack.empty()) return false;
    conditionalScopeReadStatusStack.pop_back();
    CheckConditionalScopeStatus();
    return true;
}


StackOperator::StackOperator(
    StackType& stack,
    StackType& altstack,
    const unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): stack_(stack)
    , altstack_(altstack)
    , flags_(flags)
    , conditionalManager_(conditionalManager)
    , fRequireMinimal_(flags_ & SCRIPT_VERIFY_MINIMALDATA)
{
}

bool StackOperator::operator()(opcodetype opcode, ScriptError* serror)
{
    return Helpers::set_error(serror, SCRIPT_ERR_BAD_OPCODE);
}
bool StackOperator::operator()(opcodetype opcode, CScript scriptCode, ScriptError* serror)
{
    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
}

valtype& StackOperator::stackTop(unsigned depth)
{
    return *(stack_.rbegin()+depth);
}
valtype& StackOperator::altstackTop(unsigned depth)
{
    return *(altstack_.rbegin()+ depth);
}

const CScriptNum StackOperator::bnZero = CScriptNum(0);
const CScriptNum StackOperator::bnOne=CScriptNum(1);
const valtype StackOperator::vchFalse =valtype(0);
const valtype StackOperator::vchTrue =valtype(1, 1);


struct DisabledOp: public StackOperator
{
    DisabledOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {
    }

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        if (flags_ & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
        {
            return Helpers::set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
        }
        return true;
    }
};

struct PushValueOp: public StackOperator
{
    PushValueOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        CScriptNum bn((int)opcode - (int)(OP_1 - 1));
        stack_.push_back(bn.getvch());
        return true;
    }
};

struct ConditionalOp: public StackOperator
{
    ConditionalOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        switch(opcode)
        {
            case OP_IF: case OP_NOTIF:
            {
                // <expression> if [statements] [else [statements]] endif
                bool fValue = false;
                if (!conditionalManager_.ConditionalScopeNeedsClosing())
                {
                    if (stack_.size() < 1)
                        return Helpers::set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                    valtype& vch = stackTop();
                    fValue = Helpers::CastToBool(vch);
                    if (opcode == OP_NOTIF)
                        fValue = !fValue;
                    stack_.pop_back();
                }
                conditionalManager_.OpenScope(fValue);
            }
            break;

            case OP_ELSE:
            {
                if (!conditionalManager_.TryElse())
                    return Helpers::set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
            }
            break;

            case OP_ENDIF:
            {
                if (!conditionalManager_.TryCLoseScope())
                    return Helpers::set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
            }
            break;
            default:
            break;
        }
        return true;
    }

};

struct StackModificationOp: public StackOperator
{
    StackModificationOp(
    StackType& stack,
    StackType& altstack,
    const unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        switch(opcode)
        {
            case OP_TOALTSTACK:
            {
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                altstack_.push_back(stackTop());
                stack_.pop_back();
            }
            break;
            case OP_FROMALTSTACK:
            {
                if (altstack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
                stack_.push_back(altstackTop());
                altstack_.pop_back();
            }
            break;

            case OP_2DROP:
            {
                // (x1 x2 -- )
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                stack_.pop_back();
                stack_.pop_back();
            }
            break;

            case OP_2DUP:
            {
                // (x1 x2 -- x1 x2 x1 x2)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch1 = stackTop(1);
                valtype vch2 = stackTop(0);
                stack_.push_back(vch1);
                stack_.push_back(vch2);
            }
            break;

            case OP_3DUP:
            {
                // (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
                if (stack_.size() < 3)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch1 = stackTop(2);
                valtype vch2 = stackTop(1);
                valtype vch3 = stackTop(0);
                stack_.push_back(vch1);
                stack_.push_back(vch2);
                stack_.push_back(vch3);
            }
            break;

            case OP_2OVER:
            {
                // (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
                if (stack_.size() < 4)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch1 = stackTop(3);
                valtype vch2 = stackTop(2);
                stack_.push_back(vch1);
                stack_.push_back(vch2);
            }
            break;

            case OP_2ROT:
            {
                // (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
                if (stack_.size() < 6)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch1 = stackTop(5);
                valtype vch2 = stackTop(4);
                stack_.erase(stack_.end()-6, stack_.end()-4);
                stack_.push_back(vch1);
                stack_.push_back(vch2);
            }
            break;

            case OP_2SWAP:
            {
                // (x1 x2 x3 x4 -- x3 x4 x1 x2)
                if (stack_.size() < 4)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                swap(stackTop(3), stackTop(1));
                swap(stackTop(2), stackTop(0));
            }
            break;

            case OP_IFDUP:
            {
                // (x - 0 | x x)
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch = stackTop();
                if (Helpers::CastToBool(vch))
                    stack_.push_back(vch);
            }
            break;

            case OP_DEPTH:
            {
                // -- stacksize
                CScriptNum bn(stack_.size());
                stack_.push_back(bn.getvch());
            }
            break;

            case OP_DROP:
            {
                // (x -- )
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                stack_.pop_back();
            }
            break;

            case OP_DUP:
            {
                // (x -- x x)
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch = stackTop();
                stack_.push_back(vch);
            }
            break;

            case OP_NIP:
            {
                // (x1 x2 -- x2)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                stack_.erase(stack_.end() - 2);
            }
            break;

            case OP_OVER:
            {
                // (x1 x2 -- x1 x2 x1)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch = stackTop(1);
                stack_.push_back(vch);
            }
            break;

            case OP_PICK: case OP_ROLL:
            {
                // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
                // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                int n = CScriptNum(stackTop(), fRequireMinimal_).getint();
                stack_.pop_back();
                if (n < 0 || n >= (int)stack_.size())
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch = stackTop(n);
                if (opcode == OP_ROLL)
                    stack_.erase(stack_.end()-n-1);
                stack_.push_back(vch);
            }
            break;

            case OP_ROT:
            {
                // (x1 x2 x3 -- x2 x3 x1)
                //  x2 x1 x3  after first swap
                //  x2 x3 x1  after second swap
                if (stack_.size() < 3)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                swap(stackTop(2), stackTop(1));
                swap(stackTop(1), stackTop(0));
            }
            break;
            case OP_SWAP:
            {
                // (x1 x2 -- x2 x1)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                swap(stackTop(1), stackTop(0));
            }
            break;
            case OP_TUCK:
            {
                // (x1 x2 -- x2 x1 x2)
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                valtype vch = stackTop(0);
                stack_.insert(stack_.end()-2, vch);
            }
            break;
            case OP_SIZE:
            {
                // (in -- in size)
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                CScriptNum bn(stackTop().size());
                stack_.push_back(bn.getvch());
            }
            break;

            default:
            break;
        }

        return true;
    }
};

struct EqualityVerificationOp: public StackOperator
{
    EqualityVerificationOp(
    StackType& stack,
    StackType& altstack,
    const unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        if(opcode == OP_VERIFY)
        {
            if (stack_.size() < 1)
                return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
            bool fValue = Helpers::CastToBool(stackTop());
            if (fValue)
                stack_.pop_back();
            else
                return Helpers::set_error(serror, SCRIPT_ERR_VERIFY);
        }
        if(opcode == OP_EQUAL || opcode == OP_EQUALVERIFY)
        {
            if (stack_.size() < 2)
                return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
            valtype& vch1 = stackTop(1);
            valtype& vch2 = stackTop(0);
            bool fEqual = (vch1 == vch2);
            // OP_NOTEQUAL is disabled because it would be too easy to say
            // something like n != 1 and have some wiseguy pass in 1 with extra
            // zero bytes after it (numerically, 0x01 == 0x0001 == 0x000001)
            //if (opcode == OP_NOTEQUAL)
            //    fEqual = !fEqual;
            stack_.pop_back();
            stack_.pop_back();
            stack_.push_back(fEqual ? vchTrue : vchFalse);
            if (opcode == OP_EQUALVERIFY)
            {
                if (fEqual)
                    stack_.pop_back();
                else
                    return Helpers::set_error(serror, SCRIPT_ERR_EQUALVERIFY);
            }
        }
        return true;
    }
};

struct MetadataOp: public StackOperator
{
    MetadataOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        return Helpers::set_error(serror, SCRIPT_ERR_OP_META);
    }
};

struct UnaryNumericOp: public StackOperator
{
    UnaryNumericOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        if (stack_.size() < 1)
            return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

        CScriptNum bn(stackTop(), fRequireMinimal_);
        switch (opcode)
        {
            case OP_1ADD:       bn += bnOne; break;
            case OP_1SUB:       bn -= bnOne; break;
            case OP_NEGATE:     bn = -bn; break;
            case OP_ABS:        if (bn < bnZero) bn = -bn; break;
            case OP_NOT:        bn = (bn == bnZero); break;
            case OP_0NOTEQUAL:  bn = (bn != bnZero); break;
            default:            return Helpers::set_error(serror,SCRIPT_ERR_UNKNOWN_ERROR); break;
        }
        stack_.pop_back();
        stack_.push_back(bn.getvch());
        return true;
    }
};

struct BinaryNumericOp: public StackOperator
{
    BinaryNumericOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        // (x1 x2 -- out)
        if (stack_.size() < 2)
            return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

        CScriptNum bn1(stackTop(1), fRequireMinimal_);
        CScriptNum bn2(stackTop(0), fRequireMinimal_);
        CScriptNum bn(0);
        switch (opcode)
        {
            case OP_ADD:
                bn = bn1 + bn2;
                break;

            case OP_SUB:
                bn = bn1 - bn2;
                break;

            case OP_BOOLAND:             bn = (bn1 != bnZero && bn2 != bnZero); break;
            case OP_BOOLOR:              bn = (bn1 != bnZero || bn2 != bnZero); break;
            case OP_NUMEQUAL:            bn = (bn1 == bn2); break;
            case OP_NUMEQUALVERIFY:      bn = (bn1 == bn2); break;
            case OP_NUMNOTEQUAL:         bn = (bn1 != bn2); break;
            case OP_LESSTHAN:            bn = (bn1 < bn2); break;
            case OP_GREATERTHAN:         bn = (bn1 > bn2); break;
            case OP_LESSTHANOREQUAL:     bn = (bn1 <= bn2); break;
            case OP_GREATERTHANOREQUAL:  bn = (bn1 >= bn2); break;
            case OP_MIN:                 bn = (bn1 < bn2 ? bn1 : bn2); break;
            case OP_MAX:                 bn = (bn1 > bn2 ? bn1 : bn2); break;
            default:                     return Helpers::set_error(serror,SCRIPT_ERR_UNKNOWN_ERROR); break;
        }

        stack_.pop_back();
        stack_.pop_back();
        stack_.push_back(bn.getvch());

        if (opcode == OP_NUMEQUALVERIFY)
        {
            if (Helpers::CastToBool(stackTop()))
                stack_.pop_back();
            else
                return Helpers::set_error(serror, SCRIPT_ERR_NUMEQUALVERIFY);
        }
        return true;
    }
};

struct NumericBoundsOp: public StackOperator
{
    NumericBoundsOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        // (x min max -- out)
        if (stack_.size() < 3)
            return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

        CScriptNum bn1(stackTop(2), fRequireMinimal_);
        CScriptNum bn2(stackTop(1), fRequireMinimal_);
        CScriptNum bn3(stackTop(0), fRequireMinimal_);
        bool fValue = (bn2 <= bn1 && bn1 < bn3);
        stack_.pop_back();
        stack_.pop_back();
        stack_.pop_back();
        stack_.push_back(fValue ? vchTrue : vchFalse);

        return true;
    }
};

struct HashingOp: public StackOperator
{
    HashingOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        // (in -- hash)
        if (stack_.size() < 1)
            return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
        valtype& vch = stackTop();
        valtype vchHash((opcode == OP_RIPEMD160 || opcode == OP_SHA1 || opcode == OP_HASH160) ? 20 : 32);
        if (opcode == OP_RIPEMD160)
            CRIPEMD160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
        else if (opcode == OP_SHA1)
            CSHA1().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
        else if (opcode == OP_SHA256)
            CSHA256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
        else if (opcode == OP_HASH160)
            CHash160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
        else if (opcode == OP_HASH256)
            CHash256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
        stack_.pop_back();
        stack_.push_back(vchHash);

        return true;
    }
};

struct SignatureCheckOp: public StackOperator
{
private:
    unsigned& opCount_;
    const BaseSignatureChecker& checker_;
public:
    SignatureCheckOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager,
        unsigned& opCount,
        const BaseSignatureChecker& checker
        ): StackOperator(stack,altstack,flags,conditionalManager)
        , opCount_(opCount)
        , checker_(checker)
    {}

    bool operator()(opcodetype opcode, CScript scriptCode, ScriptError* serror) override
    {
        // Subset of script starting at the most recent codeseparator
        //CScript scriptCode(pbegincodehash, pend);
        // (sig pubkey -- bool)
        switch(opcode)
        {
            case OP_CHECKSIG: case OP_CHECKSIGVERIFY:
            {
                if (stack_.size() < 2)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                const valtype& vchSig    = stackTop(1);
                const valtype& vchPubKey = stackTop(0);

                // Drop the signature, since there's no way for a signature to sign itself
                scriptCode.FindAndDelete(CScript(vchSig));
                if (!BaseSignatureChecker::CheckSignatureEncoding(vchSig, flags_, serror) ||
                    !BaseSignatureChecker::CheckPubKeyEncoding(vchPubKey, flags_, serror))
                {
                    return false;
                }
                bool fSuccess = checker_.CheckSig(vchSig, vchPubKey, scriptCode); // Needs to include the encoding checks at the begining

                stack_.pop_back();
                stack_.pop_back();
                stack_.push_back(fSuccess ? vchTrue : vchFalse);
                if (opcode == OP_CHECKSIGVERIFY)
                {
                    if (fSuccess)
                        stack_.pop_back();
                    else
                        return Helpers::set_error(serror, SCRIPT_ERR_CHECKSIGVERIFY);
                }
            }
            break;
            case OP_CHECKMULTISIG: case OP_CHECKMULTISIGVERIFY:
            {    // ([sig ...] num_of_signatures [pubkey ...] num_of_pubkeys -- bool)
                int i = 1;
                if ((int)stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                int nKeysCount = CScriptNum(stackTop(i-1), fRequireMinimal_).getint();
                if (nKeysCount < 0 || nKeysCount > 20)
                    return Helpers::set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
                opCount_ += static_cast<unsigned>(nKeysCount);
                if (opCount_ > MAXIMUM_NUMBER_OF_OPCODES)
                    return Helpers::set_error(serror, SCRIPT_ERR_OP_COUNT);

                int ikey = ++i;
                i += nKeysCount;
                if ((int)stack_.size() < i)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                int nSigsCount = CScriptNum(stackTop(i-1), fRequireMinimal_).getint();
                if (nSigsCount < 0 || nSigsCount > nKeysCount)
                    return Helpers::set_error(serror, SCRIPT_ERR_SIG_COUNT);
                int isig = ++i;
                i += nSigsCount;
                if ((int)stack_.size() < i)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                // Drop the signatures, since there's no way for a signature to sign itself
                for (int k = 0; k < nSigsCount; k++)
                {
                    valtype& vchSig = stackTop(isig+k-1);
                    scriptCode.FindAndDelete(CScript(vchSig));
                }

                bool fSuccess = true;
                while (fSuccess && nSigsCount > 0)
                {
                    valtype& vchSig    = stackTop(isig-1);
                    valtype& vchPubKey = stackTop(ikey-1);

                    // Note how this makes the exact order of pubkey/signature evaluation
                    // distinguishable by CHECKMULTISIG NOT if the STRICTENC flag is set.
                    // See the script_(in)valid tests for details.
                    if (!BaseSignatureChecker::CheckSignatureEncoding(vchSig, flags_, serror) ||
                        !BaseSignatureChecker::CheckPubKeyEncoding(vchPubKey, flags_, serror)) {
                        // serror is set
                        return false;
                    }

                    // Check signature
                    bool fOk = checker_.CheckSig(vchSig, vchPubKey, scriptCode);

                    if (fOk) {
                        isig++;
                        nSigsCount--;
                    }
                    ikey++;
                    nKeysCount--;

                    // If there are more signatures left than keys left,
                    // then too many signatures have failed. Exit early,
                    // without checking any further signatures.
                    if (nSigsCount > nKeysCount)
                        fSuccess = false;
                }

                // Clean up stack of actual arguments
                while (i-- > 1)
                    stack_.pop_back();

                // A bug causes CHECKMULTISIG to consume one extra argument
                // whose contents were not checked in any way.
                //
                // Unfortunately this is a potential source of mutability,
                // so optionally verify it is exactly equal to zero prior
                // to removing it from the stack.
                if (stack_.size() < 1)
                    return Helpers::set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                if ((flags_ & SCRIPT_VERIFY_NULLDUMMY) && stackTop().size())
                    return Helpers::set_error(serror, SCRIPT_ERR_SIG_NULLDUMMY);
                stack_.pop_back();

                stack_.push_back(fSuccess ? vchTrue : vchFalse);

                if (opcode == OP_CHECKMULTISIGVERIFY)
                {
                    if (fSuccess)
                        stack_.pop_back();
                    else
                        return Helpers::set_error(serror, SCRIPT_ERR_CHECKMULTISIGVERIFY);
                }
            }
            break;
            default:
            break;
        }
        return true;
    }
};

struct CoinstakeCheckOp: public StackOperator
{
private:
    const BaseSignatureChecker& checker_;
public:
    CoinstakeCheckOp(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager,
        const BaseSignatureChecker& checker
        ): StackOperator(stack,altstack,flags,conditionalManager)
        , checker_(checker)
    {}

    bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        bool success = checker_.CheckCoinstake();
        stack_.push_back(success ? vchTrue : vchFalse);
        if(!success)
            return Helpers::set_error(serror,SCRIPT_ERR_VERIFY);
        else
            stack_.pop_back();
        return true;
    }
};


namespace
{
const std::set<opcodetype> upgradableOpCodes =
    {OP_NOP1,OP_NOP2,OP_NOP3,OP_NOP4,OP_NOP5,OP_NOP6,OP_NOP7,OP_NOP8,OP_NOP9,OP_NOP10};
const std::set<opcodetype> simpleValueOpCodes =
    {OP_1NEGATE ,OP_1 ,OP_2 ,OP_3 , OP_4 , OP_5 , OP_6 , OP_7 , OP_8,
    OP_9 ,OP_10 ,OP_11 , OP_12 , OP_13 , OP_14 , OP_15 , OP_16};
const std::set<opcodetype> conditionalOpCodes = {OP_IF, OP_NOTIF, OP_ELSE,OP_ENDIF};
const std::set<opcodetype> stackModificationOpCodes = {
    OP_TOALTSTACK, OP_FROMALTSTACK, OP_2DROP, OP_2DUP, OP_3DUP, OP_2OVER, OP_2ROT,
    OP_2SWAP, OP_IFDUP, OP_DEPTH, OP_DROP, OP_DUP, OP_NIP, OP_OVER, OP_PICK, OP_ROLL,
    OP_ROT, OP_SWAP, OP_TUCK, OP_SIZE};
const std::set<opcodetype> equalityAndVerificationOpCodes =
    {OP_EQUAL,OP_EQUALVERIFY,OP_VERIFY};
const std::set<opcodetype> unaryNumericOpCodes =
    {OP_1ADD ,OP_1SUB ,OP_NEGATE, OP_ABS ,OP_NOT ,OP_0NOTEQUAL};
const std::set<opcodetype> binaryNumericOpCodes =
    {OP_ADD, OP_SUB, OP_BOOLAND, OP_BOOLOR, OP_NUMEQUAL, OP_NUMEQUALVERIFY, OP_NUMNOTEQUAL,
    OP_LESSTHAN, OP_GREATERTHAN, OP_LESSTHANOREQUAL, OP_GREATERTHANOREQUAL, OP_MIN, OP_MAX};
const std::set<opcodetype> hashingOpCodes =
    {OP_RIPEMD160, OP_SHA1, OP_SHA256, OP_HASH160, OP_HASH256};
const std::set<opcodetype> checkSigOpcodes =
    {OP_CHECKSIG, OP_CHECKSIGVERIFY, OP_CHECKMULTISIG, OP_CHECKMULTISIGVERIFY};

#define ApplyOperation(opname) \
    opname(stack,altstack,flags,conditionalManager)(opcode,serror)\

    static bool ApplyOpcode(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager,
        const BaseSignatureChecker& checker,
        opcodetype opcode,
        ScriptError* serror)
    {
        if(flags & SCRIPT_REQUIRE_COINSTAKE && opcode == OP_REQUIRE_COINSTAKE)
        {
            return CoinstakeCheckOp(stack,altstack,flags,conditionalManager,checker)(opcode,serror);
        }
        if(opcode == OP_META)
        {
            return ApplyOperation(MetadataOp);
        }
        if(opcode == OP_WITHIN)
        {
            return ApplyOperation(NumericBoundsOp);
        }
        if(upgradableOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(DisabledOp);
        }
        if(simpleValueOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(PushValueOp);
        }
        if(conditionalOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(ConditionalOp);
        }
        if(stackModificationOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(StackModificationOp);
        }
        if(equalityAndVerificationOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(EqualityVerificationOp);
        }
        if(unaryNumericOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(UnaryNumericOp);
        }
        if(binaryNumericOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(BinaryNumericOp);
        }
        if(hashingOpCodes.count(opcode) > 0)
        {
            return ApplyOperation(HashingOp);
        }

        Helpers::set_error(serror,SCRIPT_ERR_BAD_OPCODE);
        return false;
    }
    static bool ApplyOpcode(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager,
        unsigned& opCount,
        const BaseSignatureChecker& checker,
        opcodetype opcode,
        const CScript& scriptCode,
        ScriptError* serror)
    {
        if(checkSigOpcodes.count(opcode) > 0)
        {
            return SignatureCheckOp(stack,altstack,flags,conditionalManager,opCount,checker)(opcode,scriptCode,serror);
        }
        Helpers::set_error(serror,SCRIPT_ERR_INVALID_STACK_OPERATION);
        return false;
    }
};

StackOperationManager::StackOperationManager(
    StackType& stack,
    const BaseSignatureChecker& checker,
    unsigned flags
    ): stack_(stack)
    , altstack_()
    , flags_(flags)
    , conditionalManager_()
    , checker_(checker)
    , opCount_(0u)
{
}

bool StackOperationManager::ApplyOp(opcodetype opcode,ScriptError* serror)
{
    return ApplyOpcode(stack_,altstack_,flags_,conditionalManager_,checker_,opcode,serror);
}
bool StackOperationManager::ApplyOp(opcodetype opcode,const CScript& scriptCode,ScriptError* serror)
{
    return ApplyOpcode(stack_,altstack_,flags_,conditionalManager_,opCount_,checker_,opcode,scriptCode,serror);
}

bool StackOperationManager::ReserveAdditionalOp()
{
    return (++opCount_ <= MAXIMUM_NUMBER_OF_OPCODES);
}

void StackOperationManager::PushData(const valtype& stackElement)
{
    stack_.push_back(stackElement);
}

bool StackOperationManager::ConditionalNeedsClosing() const
{
    return conditionalManager_.ConditionalScopeNeedsClosing();
}
bool StackOperationManager::ConditionalScopeIsBalanced() const
{
    return conditionalManager_.StackIsEmpty();
}

unsigned StackOperationManager::TotalStackSize() const
{
    return stack_.size() + altstack_.size();
}

bool StackOperationManager::OpcodeIsDisabled(const opcodetype& opcode) const
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