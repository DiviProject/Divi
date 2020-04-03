#include <script/InterpreterStack.h>

RandomAccessStack::RandomAccessStack(): stack_()
{
}

const StackElement& RandomAccessStack::top(unsigned depth) const
{
    static StackElement defaultValue;
    if(depth < stack_.size())
    {
        return *(stack_.rbegin() + depth);
    }
    else
    {
        return defaultValue;
    }
}
void RandomAccessStack::push(const StackElement& element)
{
    return stack_.push_back(element);
}
void RandomAccessStack::pop()
{
    if(!empty())
    {
        stack_.pop_back();
    }
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
    return false;
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