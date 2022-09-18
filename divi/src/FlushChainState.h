#ifndef FLUSH_CHAIN_STATE_H
#define FLUSH_CHAIN_STATE_H
class CValidationState;
class MainNotificationSignals;
class CCriticalSection;
class ChainstateManager;
enum FlushStateMode {
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};
bool FlushStateToDisk(
    ChainstateManager& chainstate,
    CValidationState& state,
    FlushStateMode mode,
    MainNotificationSignals& mainNotificationSignals,
    CCriticalSection& mainCriticalSection);
#endif// FLUSH_CHAIN_STATE_H