#ifndef TEST_ME
#define TEST_ME

#include <gmock/gmock.h>
#include <boost/test/unit_test.hpp>

using ::testing::Return;

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

BOOST_AUTO_TEST_SUITE(CheckGoogleTestIsEnabled)

BOOST_AUTO_TEST_CASE(willPassOrFailTrivially)
{
    MyInterfaceMock mock;
    ON_CALL(mock,MyMethod()).WillByDefault(Return(4));
    BOOST_CHECK(mock.MyMethod()!=5);
    ON_CALL(mock,MyMethod()).WillByDefault(Return(5));
    BOOST_CHECK(mock.MyMethod()==5);
}

BOOST_AUTO_TEST_SUITE_END()
#endif