#ifndef INTERPRETER_STACK_H
#define INTERPRETER_STACK_H

#include <vector>
#include <opcodes.h>

typedef std::vector<unsigned char> StackElement;
typedef std::vector<StackElement> BasicStack;

class RandomAccessStack
{
private:
    BasicStack stack_;
public:
    RandomAccessStack();

    const StackElement& top(unsigned depth = 0) const;
    void push(const StackElement& element);
    void pop();
    bool empty() const;

    const BasicStack& getStack() const;
};

class InterpreterStack
{
private:
    RandomAccessStack stack_;
    RandomAccessStack altstack_;
public:
    bool PushData(const StackElement& data);
    bool ApplyOpcode(opcodetype opcode);
    const RandomAccessStack& stack() const;
    const RandomAccessStack& altstack() const;
};

#endif // INTERPRETER_STACK_H