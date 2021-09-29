#ifndef START_AND_SHUTDOWN_SIGNALS_H
#define START_AND_SHUTDOWN_SIGNALS_H
#include <boost/signals2/signal.hpp>
struct StartAndShutdownSignals
{
private:
    StartAndShutdownSignals();
public:
    boost::signals2::signal<void ()> startShutdown;
    boost::signals2::signal<bool ()> shutdownRequested;
    boost::signals2::signal<void ()> shutdown;

    static StartAndShutdownSignals& instance()
    {
        static StartAndShutdownSignals signals;
        return signals;
    }
};
#endif// START_AND_SHUTDOWN_SIGNALS_H