#include <JsonParseHelpers.h>
#include <utilstrencodings.h>
#include <rpcprotocol.h>

uint256 ParseHashV(const json_spirit::Value& v, std::string strName)
{
    std::string strHex;
    if (v.type() == json_spirit::Value_type::str_type)
        strHex = v.get_str();
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
    uint256 result;
    result.SetHex(strHex);
    return result;
}
uint256 ParseHashO(const json_spirit::Object& o, std::string strKey)
{
    return ParseHashV(json_spirit::find_value(o, strKey), strKey);
}
std::vector<unsigned char> ParseHexV(const json_spirit::Value& v, std::string strName)
{
    std::string strHex = (v.type() == json_spirit::Value_type::str_type)? v.get_str(): "";
    if (!IsHex(strHex))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName + " must be hexadecimal string (not '" + strHex + "')");
    return ParseHex(strHex);
}
std::vector<unsigned char> ParseHexO(const json_spirit::Object& o, std::string strKey)
{
    return ParseHexV(json_spirit::find_value(o, strKey), strKey);
}

int ParseInt(const json_spirit::Object& o, std::string strKey)
{
    const json_spirit::Value& v = json_spirit::find_value(o, strKey);
    if (v.type() != json_spirit::Value_type::int_type)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, " + strKey + "is not an int");

    return v.get_int();
}

bool ParseBool(const json_spirit::Object& o, std::string strKey)
{
    const json_spirit::Value& v = json_spirit::find_value(o, strKey);
    if (v.type() != json_spirit::Value_type::bool_type)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, " + strKey + "is not a bool");

    return v.get_bool();
}