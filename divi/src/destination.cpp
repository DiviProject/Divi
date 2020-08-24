#include <destination.h>
#include <hash.h>
#include <script/script.h>

CScriptID::CScriptID() : uint160() {}
CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}
CScriptID::CScriptID(const uint160& in) : uint160(in) {}