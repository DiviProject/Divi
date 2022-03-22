#ifndef MOCK_DATABASE_WRAPPER_H
#define MOCK_DATABASE_WRAPPER_H
#include <gmock/gmock.h>
#include <I_DatabaseWrapper.h>
class MockDatabaseWrapper: public I_DatabaseWrapper
{
public:
    virtual ~MockDatabaseWrapper(){}
    MOCK_METHOD0(Open, bool());
    MOCK_METHOD1(Verify, DatabaseStatus(const std::string& walletFilename));
    MOCK_METHOD1(FlushToDisk, void(const std::string& walletFilename));
    MOCK_METHOD1(FilenameIsInUse, bool(const std::string& walletFilename));
    MOCK_METHOD0(GetDatabaseLock, CCriticalSection&());
};
#endif //MOCK_DATABASE_WRAPPER_H