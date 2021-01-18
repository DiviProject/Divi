
#ifndef TEST_ONLY_H
#define TEST_ONLY_H
#include <boost/test/unit_test.hpp>
#define SKIP_TEST *boost::unit_test::disabled()
#define BOOST_CHECK_EQUAL_MESSAGE(L, R, M)      { BOOST_TEST_INFO(M); BOOST_CHECK_EQUAL(L, R); }
#endif //TEST_ONLY_H