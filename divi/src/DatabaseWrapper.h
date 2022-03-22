#ifndef DATABASE_WRAPPER_H
#define DATABASE_WRAPPER_H
#include <I_DatabaseWrapper.h>
class CDBEnv;
class DatabaseWrapper: public I_DatabaseWrapper
{
private:
    CDBEnv& berkleyEnvironment_;
public:
    DatabaseWrapper(const std::string& directory);
    virtual ~DatabaseWrapper(){}
    virtual bool Open(const std::string& directory);
    virtual DatabaseStatus Verify(const std::string& walletFilename);
    virtual void FlushToDisk(const std::string& walletFilename);
    virtual bool FilenameIsInUse(const std::string& walletFilename);
    virtual CCriticalSection& GetDatabaseLock();
};
#endif //DATABASE_WRAPPER_H