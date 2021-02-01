#include <version.h>

// Note: whenever a protocol update is needed toggle between both implementations (comment out the formerly active one)
//       so we can leave the existing clients untouched (old SPORK will stay on so they don't see even older clients).
//       Those old clients won't react to the changes of the other (new) SPORK because at the time of their implementation
//       it was the one which was commented out
int ActiveProtocol()
{
    return MIN_PEER_PROTO_VERSION_AFTER_ENFORCEMENT;
}