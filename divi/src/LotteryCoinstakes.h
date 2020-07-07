#ifndef LOTTERY_COINSTAKES_H
#define LOTTERY_COINSTAKES_H
#include <vector>
#include <utility>
#include <uint256.h>
#include <script/script.h>
typedef std::vector<std::pair<uint256,CScript>> LotteryCoinstakes;
#endif //LOTTERY_COINSTAKES_H