#include <MasternodePaymentData.h>
#include <sstream>

MasternodePaymentData::MasternodePaymentData(
    ): mapMasternodePayeeVotes()
    , mapMasternodeBlocks()
{
}

MasternodePaymentData::~MasternodePaymentData()
{
}

bool MasternodePaymentData::masternodeWinnerVoteIsKnown(const uint256& hash) const
{
    const auto mit = mapMasternodePayeeVotes.find(hash);
    if (mit == mapMasternodePayeeVotes.end())
        return false;
    return true;
}

std::string MasternodePaymentData::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}