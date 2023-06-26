#ifndef NODE_SIGNALS_H
#define NODE_SIGNALS_H
#include <boost/signals2/signal.hpp>
#include <string>
#include <NodeId.h>
class CNodeState;
// Signals for message handling
class CNode;
struct CNodeSignals {
    boost::signals2::signal<int()> GetHeight;
    boost::signals2::signal<void(CNodeState&)> InitializeNode;
    boost::signals2::signal<void(NodeId)> FinalizeNode;
    boost::signals2::signal<bool(CNode*)> ProcessReceivedMessages;
    boost::signals2::signal<bool(CNode*,bool)> SendMessages;
    boost::signals2::signal<void(CNode*)> RespondToRequestForDataFrom;
    boost::signals2::signal<void(CNode*)> AdvertizeLocalAddress;
};
#endif// NODE_SIGNALS_H