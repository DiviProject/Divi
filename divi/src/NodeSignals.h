#ifndef NODE_SIGNALS_H
#define NODE_SIGNALS_H
#include <boost/signals2/signal.hpp>
#include <string>
#include <NodeId.h>
class CNodeState;
// Signals for message handling
class CNode;
struct CNodeSignals {
    boost::signals2::signal<void(NodeId, CNodeState&)> InitializeNode;
    boost::signals2::signal<void(NodeId)> FinalizeNode;
};
#endif// NODE_SIGNALS_H