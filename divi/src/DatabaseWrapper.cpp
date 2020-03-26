#include <DatabaseWrapper.h>
#include <db.h>
#include <map>

typedef std::map<CDBEnv::VerifyResult,I_DatabaseWrapper::DatabaseStatus> VerificationCodeMapping;
const VerificationCodeMapping conversionTable = {
    {CDBEnv::VerifyResult::VERIFY_OK, I_DatabaseWrapper::OK},
    {CDBEnv::VerifyResult::RECOVER_OK, I_DatabaseWrapper::RECOVERED_OK},
    {CDBEnv::VerifyResult::RECOVER_FAIL, I_DatabaseWrapper::RECOVERY_FAIL}
};

bool DatabaseWrapper::Open(const std::string& directory)
{
    return CDB::bitdb.Open(directory);
}

DatabaseWrapper::DatabaseStatus DatabaseWrapper::Verify(const std::string& walletFilename)
{
    return conversionTable.at(CDB::bitdb.Verify(walletFilename,NULL));
}