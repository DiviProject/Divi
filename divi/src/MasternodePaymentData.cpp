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

std::string MasternodePaymentData::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}