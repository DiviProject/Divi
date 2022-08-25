#include <MainNotificationRegistration.h>

#include <NotificationInterface.h>

NotificationInterfaceRegistry registry;
MainNotificationSignals& g_signals = registry.getSignals();

void TemporaryMainNotifications::RegisterMainNotificationInterface(NotificationInterface* pwalletIn)
{
    registry.RegisterMainNotificationInterface(pwalletIn);
}

void TemporaryMainNotifications::UnregisterMainNotificationInterface(NotificationInterface* pwalletIn)
{
    registry.UnregisterMainNotificationInterface(pwalletIn);
}

void TemporaryMainNotifications::UnregisterAllMainNotificationInterfaces()
{
    registry.UnregisterAllMainNotificationInterfaces();
}

MainNotificationSignals& TemporaryMainNotifications::GetMainNotificationInterface()
{
    return g_signals;
}