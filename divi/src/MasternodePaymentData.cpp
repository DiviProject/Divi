#include <MasternodePaymentData.h>
#include <sstream>

MasternodePaymentData::MasternodePaymentData(
    ): mapMasternodePayeeVotes()
    , mapMasternodeBlocks()
    , cs_mapMasternodeBlocks()
    , cs_mapMasternodePayeeVotes()
{
}

MasternodePaymentData::~MasternodePaymentData()
{
}

bool MasternodePaymentData::winnerIsKnown(const uint256& hash) const
{
    LOCK(cs_mapMasternodePayeeVotes);
    const auto mit = mapMasternodePayeeVotes.find(hash);
    if (mit == mapMasternodePayeeVotes.end())
        return false;
    return true;
}
bool MasternodePaymentData::recordWinner(const CMasternodePaymentWinner& mnw)
{
    LOCK(cs_mapMasternodePayeeVotes);
    return mapMasternodePayeeVotes.emplace(mnw.GetHash(),mnw).second;
}
const CMasternodePaymentWinner& MasternodePaymentData::getKnownWinner(const uint256& winnerHash) const
{
    LOCK(cs_mapMasternodePayeeVotes);
    return mapMasternodePayeeVotes.find(winnerHash)->second;
}
void MasternodePaymentData::pruneOutdatedMasternodeWinners(const int currentChainHeight)
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
    constexpr int blockDepthToKeepWinnersAroundFor = 1000;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (currentChainHeight - winner.GetHeight() > blockDepthToKeepWinnersAroundFor) {
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.GetScoreHash());
        } else {
            ++it;
        }
    }
}

std::string MasternodePaymentData::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}