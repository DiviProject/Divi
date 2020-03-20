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
};
#endif //MOCK_DATABASE_WRAPPER_H