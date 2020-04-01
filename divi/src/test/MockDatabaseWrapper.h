#ifndef MOCK_DATABASE_WRAPPER_H
#define MOCK_DATABASE_WRAPPER_H
#include <gmock/gmock.h>
#include <i_databaseWrapper.h>
class MockDatabaseWrapper: public I_DatabaseWrapper
{
public:
    virtual ~MockDatabaseWrapper(){}
    MOCK_METHOD1(Open, bool(const std::string& directory));
    MOCK_METHOD1(Verify, DatabaseStatus(const std::string& walletFilename));
    MOCK_METHOD1(Dettach, void(const std::string& walletFilename));
    MOCK_METHOD1(FilenameIsInUse, bool(const std::string& walletFilename));
    MOCK_METHOD0(GetDatabaseLock, CCriticalSection&());
};
#endif //MOCK_DATABASE_WRAPPER_H