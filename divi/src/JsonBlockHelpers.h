#ifndef JSON_BLOCK_HELPERS_H
#define JSON_BLOCK_HELPERS_H
#include <json/json_spirit.h>
class CBlockIndex;
class CBlock;
double GetDifficulty(const CBlockIndex* blockindex = nullptr);
json_spirit::Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);
json_spirit::Object blockHeaderToJSON(const CBlock& block, const CBlockIndex* blockindex);
#endif// JSON_BLOCK_HELPERS_H