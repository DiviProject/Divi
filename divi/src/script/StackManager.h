#include <script/opcodes.h>
#include <vector>
#include <script/script.h>
#include <script/script_error.h>
#include <algorithm>
#include <memory>


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
    unsigned& flags_;
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
        unsigned& flags,
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
    static const std::set<opcodetype> upgradableOpCodes;
    static const std::set<opcodetype> simpleValueOpCodes;
    static const std::set<opcodetype> conditionalOpCodes;
    static const std::set<opcodetype> stackModificationOpCodes;
    static const std::set<opcodetype> equalityAndVerificationOpCodes;
    static const std::set<opcodetype> unaryNumericOpCodes;
    static const std::set<opcodetype> binaryNumericOpCodes;
    static const std::set<opcodetype> hashingOpCodes;
    static const std::set<opcodetype> checkSigOpcodes;

    StackType& stack_;
    const BaseSignatureChecker& checker_;
    StackType altstack_;
    unsigned flags_;
    unsigned opCount_;

    ConditionalScopeStackManager conditionalManager_;
    StackOperator defaultOperation_;
    std::map<opcodetype, StackOperator*> stackOperationMapping_;

    std::shared_ptr<StackOperator> disableOp_;
    std::shared_ptr<StackOperator> pushValueOp_;
    std::shared_ptr<StackOperator> conditionalOp_;
    std::shared_ptr<StackOperator> stackModificationOp_;
    std::shared_ptr<StackOperator> equalityVerificationOp_;
    std::shared_ptr<StackOperator> metadataOp_;
    std::shared_ptr<StackOperator> unaryNumericOp_;
    std::shared_ptr<StackOperator> binaryNumericOp_;
    std::shared_ptr<StackOperator> numericBoundsOp_;
    std::shared_ptr<StackOperator> hashingOp_;
    std::shared_ptr<StackOperator> checksigOp_;
    std::shared_ptr<StackOperator> checkCoinstakeOp_;

    void InitMapping();
public:
    StackOperationManager(
        StackType& stack,
        const BaseSignatureChecker& checker,
        unsigned flags
        );

    StackOperator* GetOp(opcodetype opcode);
    bool HasOp(opcodetype opcode) const;
    bool ReserveAdditionalOp();
    void PushData(const valtype& stackElement);

    bool ConditionalNeedsClosing() const;
    bool ConditionalScopeIsBalanced() const;
    unsigned TotalStackSize() const;

    bool OpcodeIsDisabled(const opcodetype& opcode) const;
};
