#ifndef DATABASE_WRAPPER_H
#define DATABASE_WRAPPER_H
#include <I_DatabaseWrapper.h>
class CDBEnv;
class DatabaseWrapper final: public I_DatabaseWrapper
{
private:
    CDBEnv& berkleyEnvironment_;
public:
    DatabaseWrapper(const std::string& directory);
    virtual ~DatabaseWrapper(){}
    bool Open(const std::string& directory) override;
    DatabaseStatus Verify(const std::string& walletFilename) override;
    void FlushToDisk(const std::string& walletFilename) override;
    bool FilenameIsInUse(const std::string& walletFilename) override;
    CCriticalSection& GetDatabaseLock() override;
};
#endif //DATABASE_WRAPPER_H