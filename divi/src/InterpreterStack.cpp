#include <InterpreterStack.h>

RandomAccessStack::RandomAccessStack(): stack_()
{
}

const StackElement& RandomAccessStack::top(unsigned depth = 0) const
{
    return *(stack_.rbegin() + depth);
}
void RandomAccessStack::push(const StackElement& element)
{
    return stack_.push_back(element);
}
void RandomAccessStack::pop()
{
    stack_.pop_back();
}

const BasicStack& RandomAccessStack::getStack() const
{
    return stack_;
}

bool RandomAccessStack::empty() const
{
    return stack_.empty();
}


// Interpreter Stack functionality

// Interface functionality
bool InterpreterStack::PushData(const StackElement& data)
{
}

bool InterpreterStack::ApplyOpcode(opcodetype opcode)
{
    return false;
}
const RandomAccessStack& InterpreterStack::stack() const
{
    return stack_;
}
const RandomAccessStack& InterpreterStack::altstack() const
{
    return altstack_;
}