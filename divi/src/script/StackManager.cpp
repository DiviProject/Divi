#include <script/StackManager.h>

#include <script/opcodes.h>
#include <vector>
#include <script/script.h>
#include <script/script_error.h>
#include <script/scriptandsigflags.h>
#include <algorithm>
#include <memory>

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
    return ConditionalScopeStackManager::conditionalScopeNeedsClosing;
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
    unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): stack_(stack)
    , altstack_(altstack)
    , flags_(flags)
    , conditionalManager_(conditionalManager)
{
}

bool StackOperator::operator()(opcodetype opcode, ScriptError* serror)
{
    return Helpers::set_error(serror, SCRIPT_ERR_BAD_OPCODE);
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
const CScriptNum StackOperator::bnFalse=CScriptNum(0);
const CScriptNum StackOperator::bnTrue=CScriptNum(1);
const valtype StackOperator::vchFalse =valtype(0);
const valtype StackOperator::vchZero =valtype(0);
const valtype StackOperator::vchTrue =valtype(1, 1);


struct DisabledOp: public StackOperator
{
    DisabledOp(
        StackType& stack, 
        StackType& altstack, 
        unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {
    }

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
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
        unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
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
        unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
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
    unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
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
                
                bool fRequireMinimal = (flags_ & SCRIPT_VERIFY_MINIMALDATA) != 0;
                int n = CScriptNum(stackTop(), fRequireMinimal).getint();
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
    unsigned& flags,
    ConditionalScopeStackManager& conditionalManager
    ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
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
        unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        ): StackOperator(stack,altstack,flags,conditionalManager)
    {}

    virtual bool operator()(opcodetype opcode, ScriptError* serror) override
    {
        return Helpers::set_error(serror, SCRIPT_ERR_OP_META);
    }
};

const std::set<opcodetype> StackOperationManager::upgradableOpCodes = 
    {OP_NOP1,OP_NOP2,OP_NOP3,OP_NOP4,OP_NOP5,OP_NOP6,OP_NOP7,OP_NOP8,OP_NOP9,OP_NOP10};
const std::set<opcodetype> StackOperationManager::simpleValueOpCodes = 
    {OP_1NEGATE ,OP_1 ,OP_2 ,OP_3 , OP_4 , OP_5 , OP_6 , OP_7 , OP_8,
    OP_9 ,OP_10 ,OP_11 , OP_12 , OP_13 , OP_14 , OP_15 , OP_16};
const std::set<opcodetype> StackOperationManager::conditionalOpCodes = {OP_IF, OP_NOTIF, OP_ELSE,OP_ENDIF};
const std::set<opcodetype> StackOperationManager::stackModificationOpCodes = {
    OP_TOALTSTACK, OP_FROMALTSTACK, OP_2DROP, OP_2DUP, OP_3DUP, OP_2OVER, OP_2ROT, 
    OP_2SWAP, OP_IFDUP, OP_DEPTH, OP_DROP, OP_DUP, OP_NIP, OP_OVER, OP_PICK, OP_ROLL,
    OP_ROT, OP_SWAP, OP_TUCK, OP_SIZE};
const std::set<opcodetype> StackOperationManager::equalityAndVerificationOpCodes =
    {OP_EQUAL,OP_EQUALVERIFY,OP_VERIFY};

StackOperationManager::StackOperationManager(
    StackType& stack,
    StackType& altstack,
    unsigned flags
    ): stack_(stack)
    , altstack_(altstack) 
    , flags_(flags)
    , conditionalManager_()
    , defaultOperation_(stack,altstack,flags,conditionalManager_)
    , stackOperationMapping_()
    , disableOp_(std::make_shared<DisabledOp>(stack_,altstack_,flags_,conditionalManager_))
    , pushValueOp_(std::make_shared<PushValueOp>(stack_,altstack_,flags_,conditionalManager_))
    , conditionalOp_(std::make_shared<ConditionalOp>(stack_,altstack_,flags_,conditionalManager_))
    , stackModificationOp_(std::make_shared<StackModificationOp>(stack_,altstack_,flags_,conditionalManager_))
    , equalityVerificationOp_(std::make_shared<EqualityVerificationOp>(stack_,altstack_,flags_,conditionalManager_))
    , metadataOp_(std::make_shared<MetadataOp>(stack_,altstack_,flags_,conditionalManager_))
{
    InitMapping();
}

void StackOperationManager::InitMapping()
{
    for(const opcodetype& opcode: upgradableOpCodes)
    {
        stackOperationMapping_.insert({opcode, disableOp_.get() });
    }
    for(const opcodetype& opcode: simpleValueOpCodes)
    {
        stackOperationMapping_.insert({opcode, pushValueOp_.get() });
    }
    for(const opcodetype& opcode: conditionalOpCodes)
    {
        stackOperationMapping_.insert({opcode, conditionalOp_.get() });
    }
    for(const opcodetype& opcode: stackModificationOpCodes)
    {
        stackOperationMapping_.insert({opcode, stackModificationOp_.get() });
    }
    for(const opcodetype& opcode: equalityAndVerificationOpCodes)
    {
        stackOperationMapping_.insert({opcode, equalityVerificationOp_.get() });
    }
    stackOperationMapping_.insert({OP_META, metadataOp_.get()});
}

StackOperator* StackOperationManager::GetOp(opcodetype opcode)
{
    auto it = stackOperationMapping_.find(opcode);
    if(it != stackOperationMapping_.end())
    {
        return it->second;
    }
    return &defaultOperation_;
}

bool StackOperationManager::ConditionalNeedsClosing() const
{
    return conditionalManager_.ConditionalScopeNeedsClosing();
}
bool StackOperationManager::ConditionalScopeIsBalanced() const
{
    return conditionalManager_.StackIsEmpty();
}