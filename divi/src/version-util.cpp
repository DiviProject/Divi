#include <version.h>

#include <Logging.h>

static int version = MIN_PEER_PROTO_VERSION_AFTER_ENFORCEMENT;
const int& PROTOCOL_VERSION(version);

void SetProtocolVersion(const int newVersion)
{
    LogPrintf("Setting protocol version to %d from %d\n", newVersion, PROTOCOL_VERSION);
    version = newVersion;
}
