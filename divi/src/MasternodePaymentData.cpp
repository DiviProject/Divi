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
    CMasternodeBlockPayees* payees;
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

        if(winnerIsKnown(mnw.GetHash())) return false;
        assert(mapMasternodePayeeVotes.emplace(mnw.GetHash(),mnw).second);

        payees = getPayeesForScoreHash(mnw.GetScoreHash());
        if (payees == nullptr) {
            CMasternodeBlockPayees blockPayees(mnw.GetHeight());
            auto mit = mapMasternodeBlocks.emplace(mnw.GetScoreHash(), std::move(blockPayees)).first;
            payees = &mit->second;
        }
    }

    payees->CountVote(mnw.vinMasternode.prevout, mnw.payee);
    return true;
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
/** Retrieves the payees for the given block.  Returns null if there is
 *  no matching entry.  */
const CMasternodeBlockPayees* MasternodePaymentData::getPayeesForScoreHash(const uint256& hash) const
{
    return const_cast<MasternodePaymentData*>(this)->getPayeesForScoreHash(hash);
}
CMasternodeBlockPayees* MasternodePaymentData::getPayeesForScoreHash(const uint256& hash)
{
    const auto mit = mapMasternodeBlocks.find(hash);
    if (mit == mapMasternodeBlocks.end())
        return nullptr;
    return &mit->second;
}

std::string MasternodePaymentData::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}