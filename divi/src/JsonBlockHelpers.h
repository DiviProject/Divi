#ifndef JSON_BLOCK_HELPERS_H
#define JSON_BLOCK_HELPERS_H
#include <json/json_spirit.h>
class CBlockIndex;
class CBlock;
class CChain;
double GetDifficulty(const CChain& activeChain, const CBlockIndex* blockindex = nullptr);
json_spirit::Object blockToJSON(const CChain& activeChain, const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);
json_spirit::Object blockHeaderToJSON(const CBlock& block, const CBlockIndex* blockindex);
#endif// JSON_BLOCK_HELPERS_H