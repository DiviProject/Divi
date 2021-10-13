#include <DatabaseWrapper.h>
#include <dbenv.h>
#include <map>

typedef std::map<CDBEnv::VerifyResult,I_DatabaseWrapper::DatabaseStatus> VerificationCodeMapping;
const VerificationCodeMapping conversionTable = {
    {CDBEnv::VerifyResult::VERIFY_OK, I_DatabaseWrapper::OK},
    {CDBEnv::VerifyResult::RECOVER_OK, I_DatabaseWrapper::RECOVERED_OK},
    {CDBEnv::VerifyResult::RECOVER_FAIL, I_DatabaseWrapper::RECOVERY_FAIL}
};

bool DatabaseWrapper::Open(const std::string& directory)
{
    return BerkleyDBEnvWrapper().Open(directory);
}

DatabaseWrapper::DatabaseStatus DatabaseWrapper::Verify(const std::string& walletFilename)
{
    return conversionTable.at(BerkleyDBEnvWrapper().Verify(walletFilename,NULL));
}

void DatabaseWrapper::Dettach(const std::string& walletFilename)
{
    BerkleyDBEnvWrapper().CloseDb(walletFilename);
    BerkleyDBEnvWrapper().CheckpointLSN(walletFilename);
    BerkleyDBEnvWrapper().mapFileUseCount.erase(walletFilename);
}

bool DatabaseWrapper::FilenameIsInUse(const std::string& walletFilename)
{
    auto it = BerkleyDBEnvWrapper().mapFileUseCount.find( walletFilename );
    return (
        BerkleyDBEnvWrapper().mapFileUseCount.count(walletFilename) > 0 &&
        ((it != BerkleyDBEnvWrapper().mapFileUseCount.end())?  (it->second > 0)  :  false)
    );
}

CCriticalSection& DatabaseWrapper::GetDatabaseLock()
{
    return BerkleyDBEnvWrapper().cs_db;
}