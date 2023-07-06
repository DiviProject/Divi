#include <MainNotificationRegistration.h>

#include <NotificationInterface.h>

NotificationInterfaceRegistry registry;
MainNotificationSignals& g_signals = registry.getSignals();

void RegisterMainNotificationInterface(NotificationInterface* pwalletIn)
{
    registry.RegisterMainNotificationInterface(pwalletIn);
}

void UnregisterMainNotificationInterface(NotificationInterface* pwalletIn)
{
    registry.UnregisterMainNotificationInterface(pwalletIn);
}

void UnregisterAllMainNotificationInterfaces()
{
    registry.UnregisterAllMainNotificationInterfaces();
}

MainNotificationSignals& GetMainNotificationInterface()
{
    return g_signals;
}