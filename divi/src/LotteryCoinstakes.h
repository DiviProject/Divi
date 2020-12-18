#ifndef LOTTERY_COINSTAKES_H
#define LOTTERY_COINSTAKES_H
#include <vector>
#include <utility>
#include <uint256.h>
#include <script/script.h>
#include <memory>
#include <serialize.h>
typedef std::pair<uint256,CScript> LotteryCoinstake;
typedef std::vector<LotteryCoinstake> LotteryCoinstakes;

struct LotteryCoinstakeData
{
public:
    std::shared_ptr<LotteryCoinstakes> storage;
    int heightOfDataStorage;
    bool storageIsLocal;

public:

    LotteryCoinstakeData();
    LotteryCoinstakeData(
        int height,
        const LotteryCoinstakes& coinstakes = LotteryCoinstakes());

    bool IsValid() const;
    void MarkAsShallowStorage();
    int height() const;
    const LotteryCoinstakes& getLotteryCoinstakes() const;
    void updateShallowDataStore(LotteryCoinstakeData& other);
    void clear();
    LotteryCoinstakeData getShallowCopy() const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(storageIsLocal);
        if(storageIsLocal)
        {
            if(!ser_action.ForRead())
            {
                LotteryCoinstakes& coinstakes = *storage;
                READWRITE(coinstakes);
            }
            else
            {
                storage.reset();
                storage = std::make_shared<LotteryCoinstakes>();
                LotteryCoinstakes& coinstakes = *storage;
                READWRITE(coinstakes);
            }

        }
        READWRITE(heightOfDataStorage);
    }
};
#endif //LOTTERY_COINSTAKES_H