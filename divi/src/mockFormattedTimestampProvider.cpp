#include <mockFormattedTimestampProvider.h>

std::string MockFormattedTimestampProvider::currentTimeStamp() const
{
    static unsigned timestampIndex = 0;
    return timestampIndex < currentTimeStampMapping.size()? currentTimeStampMapping[timestampIndex++]: std::to_string(timestampIndex);
}
void MockFormattedTimestampProvider::addCurrentTimestampMapping (std::string timestamp)
{
    currentTimeStampMapping.push_back(timestamp);
}