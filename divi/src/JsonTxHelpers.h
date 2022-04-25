#ifndef JSON_TX_HELPERS_H
#define JSON_TX_HELPERS_H
#include <uint256.h>
#include <json/json_spirit_value.h>
class CTransaction;
void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
#endif// JSON_TX_HELPERS_H