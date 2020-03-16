#ifndef I_FORMATTEDTIMESTAMPPROVIDER_H
#define I_FORMATTEDTIMESTAMPPROVIDER_H

#include <string>


class I_FormattedTimestampProvider
{
public:
    virtual ~I_FormattedTimestampProvider(){};

    virtual std::string currentTimeStamp() const = 0;
};

#endif //I_FORMATTEDTIMESTAMPPROVIDER_H