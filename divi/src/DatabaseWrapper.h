#ifndef DATABASE_WRAPPER_H
#define DATABASE_WRAPPER_H
#include <i_databaseWrapper.h>

class DatabaseWrapper: public I_DatabaseWrapper
{
public:
    virtual ~DatabaseWrapper(){}
    virtual bool Open(const std::string& directory);
    virtual DatabaseStatus Verify(const std::string& walletFilename);
};
#endif //DATABASE_WRAPPER_H