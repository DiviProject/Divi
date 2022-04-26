#ifndef JSON_PARSE_HELPERS_H
#define JSON_PARSE_HELPERS_H
#include <uint256.h>
#include <json/json_spirit.h>
/**
 * Utilities: convert hex-encoded Values
 * (throws error if not hex).
 */
uint256 ParseHashV(const json_spirit::Value& v, std::string strName);
uint256 ParseHashO(const json_spirit::Object& o, std::string strKey);
std::vector<unsigned char> ParseHexV(const json_spirit::Value& v, std::string strName);
std::vector<unsigned char> ParseHexO(const json_spirit::Object& o, std::string strKey);
int ParseInt(const json_spirit::Object& o, std::string strKey);
bool ParseBool(const json_spirit::Object& o, std::string strKey);
#endif// JSON_PARSE_HELPERS_H