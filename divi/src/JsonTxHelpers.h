#ifndef JSON_TX_HELPERS_H
#define JSON_TX_HELPERS_H
#include <uint256.h>
#include <json/json_spirit_value.h>
#include <amount.h>
class CTransaction;
class CScript;
void ScriptPubKeyToJSON(const CScript& scriptPubKey, json_spirit::Object& out, bool fIncludeHex);
void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
CAmount AmountFromValue(const json_spirit::Value& value, bool allowZero = false);
#endif// JSON_TX_HELPERS_H