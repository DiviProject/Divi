#ifndef NODE_SIGNALS_H
#define NODE_SIGNALS_H
#include <boost/signals2/signal.hpp>
#include <string>
#include <NodeId.h>
class CAddress;
// Signals for message handling
class CNode;
struct CNodeSignals {
    boost::signals2::signal<bool(CNode*, bool)> SendMessages;
    boost::signals2::signal<void(NodeId, const std::string, const CAddress&)> InitializeNode;
    boost::signals2::signal<void(NodeId)> FinalizeNode;
};
#endif// NODE_SIGNALS_H