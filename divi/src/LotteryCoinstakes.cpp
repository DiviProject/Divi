#include <LotteryCoinstakes.h>

LotteryCoinstakeData::LotteryCoinstakeData(
    ): storage(std::make_shared<LotteryCoinstakes>())
    , heightOfDataStorage(0)
    , storageIsLocal(true)
{
}
LotteryCoinstakeData::LotteryCoinstakeData(
    int height,
    const LotteryCoinstakes& coinstakes
    ): storage(std::make_shared<LotteryCoinstakes>(coinstakes))
    , heightOfDataStorage(height)
    , storageIsLocal(true)
{
}

bool LotteryCoinstakeData::IsValid() const
{
    return static_cast<bool>(storage.get());
}
void LotteryCoinstakeData::MarkAsShallowStorage()
{
    storageIsLocal = false;
}


int LotteryCoinstakeData::height() const
{
    return heightOfDataStorage;
}

const LotteryCoinstakes& LotteryCoinstakeData::getLotteryCoinstakes() const
{
    return *storage;
}
void LotteryCoinstakeData::updateShallowDataStore(LotteryCoinstakeData& other)
{
    if(!storageIsLocal && other.storageIsLocal && other.height() == heightOfDataStorage)
    {
        storage.reset();
        storage = other.storage;
    }
}

void LotteryCoinstakeData::clear()
{
    heightOfDataStorage =0;
    storageIsLocal = true;
    storage = std::make_shared<LotteryCoinstakes>();
}