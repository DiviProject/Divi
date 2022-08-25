#ifndef MAIN_NOTIFICATION_REGISTRATION_H
#define MAIN_NOTIFICATION_REGISTRATION_H
class NotificationInterface;
class MainNotificationSignals;

/** Register a wallet to receive updates from core */
void RegisterMainNotificationInterface(NotificationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterMainNotificationInterface(NotificationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllMainNotificationInterfaces();

MainNotificationSignals& GetMainNotificationInterface();
#endif// MAIN_NOTIFICATION_REGISTRATION_H