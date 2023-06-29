#ifndef STACK_MANAGER_H
#define STACK_MANAGER_H
#include <script/opcodes.h>
#include <vector>
#include <script/script.h>
#include <script/script_error.h>
#include <algorithm>
#include <memory>
#include <set>
#include <map>
#include <amount.h>


class BaseSignatureChecker;

namespace Helpers
{
    bool CastToBool(const valtype& vch);
    inline bool set_error(ScriptError* ret, const ScriptError serror);
}

 typedef std::vector<valtype> StackType;

class ConditionalScopeStackManager
{
private:
    std::vector<bool> conditionalScopeReadStatusStack;
    bool conditionalScopeNeedsClosing = false;

    void CheckConditionalScopeStatus();
public:
    bool StackIsEmpty() const;
    bool ConditionalScopeNeedsClosing() const;

    void OpenScope(bool conditionStatus);
    bool TryElse();
    bool TryCLoseScope();
};


struct StackOperator
{
protected:
    StackType& stack_;
    StackType& altstack_;
    const unsigned& flags_;
    ConditionalScopeStackManager& conditionalManager_;
    bool fRequireMinimal_;
public:
    static const CScriptNum bnZero;
    static const CScriptNum bnOne;
    static const valtype vchFalse;
    static const valtype vchTrue;

    StackOperator(
        StackType& stack,
        StackType& altstack,
        const unsigned& flags,
        ConditionalScopeStackManager& conditionalManager
        );

    virtual bool operator()(opcodetype opcode, ScriptError* serror);
    virtual bool operator()(opcodetype opcode, CScript scriptCode, ScriptError* serror);

    valtype& stackTop(unsigned depth = 0);
    valtype& altstackTop(unsigned depth = 0);
};


struct StackOperationManager
{
private:
    StackType& stack_;
    StackType altstack_;
    unsigned flags_;
    ConditionalScopeStackManager conditionalManager_;
    const BaseSignatureChecker& checker_;
    const CAmount amountBeingSpent_;
    unsigned opCount_;

public:
    StackOperationManager(
        StackType& stack,
        const BaseSignatureChecker& checker,
        const CAmount amountBeingSpent,
        unsigned flags
        );

    bool ApplyOp(opcodetype opcode,ScriptError* serror);
    bool ApplyOp(opcodetype opcode,const CScript& scriptCode,ScriptError* serror);
    bool ReserveAdditionalOp();
    void PushData(const valtype& stackElement);

    bool ConditionalNeedsClosing() const;
    bool ConditionalScopeIsBalanced() const;
    unsigned TotalStackSize() const;

    bool OpcodeIsDisabled(const opcodetype& opcode) const;
};
#endif // STACK_MANAGER_H