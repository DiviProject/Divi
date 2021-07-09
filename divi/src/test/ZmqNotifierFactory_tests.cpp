#include <zmq/zmqpublishnotifier.h>
#include <test/test_only.h>


BOOST_AUTO_TEST_SUITE(ZmqNotifierFactory_tests)

BOOST_AUTO_TEST_CASE(willConstructAnObjectForAllTheKnownNotifierTypes)
{
    for(const std::string& notifierType: GetZMQNotifierTypes())
    {
        auto* notifier = CreateNotifier(notifierType);
        BOOST_CHECK(notifier);
        delete notifier;
    }
}

BOOST_AUTO_TEST_SUITE_END()
