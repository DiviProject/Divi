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

    LotteryCoinstakeData(
        ): storage(std::make_shared<LotteryCoinstakes>())
        , heightOfDataStorage(0)
        , storageIsLocal(true)
    {
    }
    LotteryCoinstakeData(
        int height,
        const LotteryCoinstakes& coinstakes
        ): storage(std::make_shared<LotteryCoinstakes>(coinstakes))
        , heightOfDataStorage(height)
        , storageIsLocal(true)
    {
    }

    bool IsValid() const
    {
        return static_cast<bool>(storage.get());
    }
    void MarkAsShallowStorage()
    {
        storageIsLocal = false;
    }

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

    int height() const
    {
        return heightOfDataStorage;
    }

    const LotteryCoinstakes& getLotteryCoinstakes() const
    {
        return *storage;
    }
    void updateShallowDataStore(LotteryCoinstakeData& other)
    {
        if(!storageIsLocal && other.storageIsLocal && other.height() == heightOfDataStorage)
        {
            storage.reset();
            storage = other.storage;
        }
    }

    void clear()
    {
        heightOfDataStorage =0;
        storageIsLocal = true;
        storage = std::make_shared<LotteryCoinstakes>();
    }
};
#endif //LOTTERY_COINSTAKES_H