
#ifndef TEST_ONLY_H
#define TEST_ONLY_H
#include <boost/test/unit_test.hpp>
#define SKIP_TEST *boost::unit_test::disabled()
#endif