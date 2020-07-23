#ifndef LOTTERY_COINSTAKES_H
#define LOTTERY_COINSTAKES_H
#include <vector>
#include <utility>
#include <uint256.h>
#include <script/script.h>
typedef std::pair<uint256,CScript> LotteryCoinstake;
typedef std::vector<LotteryCoinstake> LotteryCoinstakes;
#endif //LOTTERY_COINSTAKES_H