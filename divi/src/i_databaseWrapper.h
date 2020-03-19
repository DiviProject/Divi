#ifndef I_DATABASE_WRAPPER_H
#define I_DATABASE_WRAPPER_H
#include <string>
class I_DatabaseWrapper
{
public:
    virtual ~I_DatabaseWrapper(){}
    virtual bool Open(const std::string& directory) = 0;
    virtual bool Verify(const std::string& walletFilename) = 0;
};
#endif //I_DATABASE_WRAPPER_H