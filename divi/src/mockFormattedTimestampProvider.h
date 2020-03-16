#include <i_formattedTimestampProvider.h>
#include <string>
#include <vector>
#include <gmock/gmock.h>

class MockFormattedTimestampProvider : public I_FormattedTimestampProvider
{
public:
    MockFormattedTimestampProvider();
    virtual ~MockFormattedTimestampProvider();
    
    MOCK_CONST_METHOD0(currentTimeStamp, std::string());
};