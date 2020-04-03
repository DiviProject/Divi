#include <gmock/gmock.h>
#include <script/InterpreterStack.h>
#include <test_only.h>

BOOST_AUTO_TEST_SUITE(InterpreterStackTests)

BOOST_AUTO_TEST_CASE(StackIsByDefaultEmpty)
{
    RandomAccessStack stack;
    EXPECT_TRUE(stack.empty());
}
BOOST_AUTO_TEST_CASE(WillNotThrowExceptionTryingToRemoveFromAnEmptyStack)
{
    RandomAccessStack stack;
    EXPECT_NO_THROW(stack.pop());
}

StackElement createRandomElement()
{
    unsigned char values[] = "abc";
    StackElement stackElement(values, values+3);
    return stackElement;
}

BOOST_AUTO_TEST_CASE(WillPushElementToTop)
{
    RandomAccessStack stack;
    StackElement stackElement = createRandomElement();
    stack.push(stackElement);
    EXPECT_TRUE(stackElement==stack.top());
}


BOOST_AUTO_TEST_SUITE_END()
