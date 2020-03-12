#include <i_formattedTimestampProvider.h>
#include <string>
#include <vector>
class MockFormattedTimestampProvider : public I_FormattedTimestampProvider
{
private:
    std::vector<std::string> currentTimeStampMapping;
public:
    void addCurrentTimestampMapping (std::string timestamp);
    virtual std::string currentTimeStamp() const;
};