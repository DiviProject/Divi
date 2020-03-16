#include <mockFormattedTimestampProvider.h>

std::string MockFormattedTimestampProvider::currentTimeStamp() const
{
    return timestampIndex < currentTimeStampMapping.size()? currentTimeStampMapping[timestampIndex++]: std::to_string(timestampIndex++);
}
void MockFormattedTimestampProvider::addCurrentTimestampMapping (std::string timestamp)
{
    currentTimeStampMapping.push_back(timestamp);
}