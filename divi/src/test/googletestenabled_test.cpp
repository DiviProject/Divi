#ifndef TEST_ME
#define TEST_ME
#include <test/gmock_boost_integration.h>

using ::testing::Return;
using ::testing::NiceMock;

class MyInterface
{
public:
    virtual ~MyInterface(){}
    virtual int MyMethod() =0;
};
class MyConcrete: public MyInterface
{
public:
    virtual ~MyConcrete(){}
    virtual int MyMethod()
    {
        return 0;
    }
};
class MyInterfaceMock: public MyInterface
{
public:
    virtual ~MyInterfaceMock(){}
    MOCK_METHOD0(MyMethod,int());
};
template <typename T>
class TestType
{
private:
    NiceMock<T> mock;
public:
    TestType(): mock()
    {
        EXPECT_CALL(mock,MyMethod()).Times(1);
    }
    void call()
    {
        mock.MyMethod();
    }
};

BOOST_AUTO_TEST_SUITE(CheckGoogleTestIsEnabled)

BOOST_AUTO_TEST_CASE(willPassOrFailTrivially)
{
    NiceMock<MyInterfaceMock> mock;
    ON_CALL(mock,MyMethod()).WillByDefault(Return(4));
    BOOST_CHECK(mock.MyMethod()!=5);
    ON_CALL(mock,MyMethod()).WillByDefault(Return(5));
    BOOST_CHECK(mock.MyMethod()==5);
}

BOOST_AUTO_TEST_CASE(willCountTheNumberOfCallsCorrectly_0)
{
    // Fails to work
    NiceMock<MyInterfaceMock> mock1;
    EXPECT_CALL(mock1,MyMethod()).Times(2);
    mock1.MyMethod();
    mock1.MyMethod();
}


BOOST_AUTO_TEST_CASE(willCountTheNumberOfCallsCorrectly_1)
{
    TestType<MyInterfaceMock> test;
    test.call();
}

BOOST_AUTO_TEST_SUITE_END()
#endif