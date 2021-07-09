#ifndef ZMQ_NOTIFIER_FACTORY_H
#define ZMQ_NOTIFIER_FACTORY_H
#include <string>
#include <vector>
class CZMQAbstractNotifier;
const std::vector<std::string>& GetZMQNotifierTypes();
CZMQAbstractNotifier* CreateNotifier(const std::string& notifierType);
#endif // ZMQ_NOTIFIER_FACTORY_H