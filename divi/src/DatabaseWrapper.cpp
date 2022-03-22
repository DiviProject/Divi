#include <DatabaseWrapper.h>
#include <dbenv.h>
#include <map>

typedef std::map<CDBEnv::VerifyResult,I_DatabaseWrapper::DatabaseStatus> VerificationCodeMapping;
const VerificationCodeMapping conversionTable = {
    {CDBEnv::VerifyResult::VERIFY_OK, I_DatabaseWrapper::OK},
    {CDBEnv::VerifyResult::RECOVER_OK, I_DatabaseWrapper::RECOVERED_OK},
    {CDBEnv::VerifyResult::RECOVER_FAIL, I_DatabaseWrapper::RECOVERY_FAIL}
};

DatabaseWrapper::DatabaseWrapper(const std::string& directory): berkleyEnvironment_(BerkleyDBEnvWrapper())
{
    Open(directory);
}

bool DatabaseWrapper::Open(const std::string& directory)
{
    return berkleyEnvironment_.Open(directory);
}

DatabaseWrapper::DatabaseStatus DatabaseWrapper::Verify(const std::string& walletFilename)
{
    return conversionTable.at(berkleyEnvironment_.Verify(walletFilename,NULL));
}

void DatabaseWrapper::Dettach(const std::string& walletFilename)
{
    berkleyEnvironment_.CloseDb(walletFilename);
    berkleyEnvironment_.CheckpointLSN(walletFilename);
    berkleyEnvironment_.mapFileUseCount.erase(walletFilename);
}

bool DatabaseWrapper::FilenameIsInUse(const std::string& walletFilename)
{
    auto it = berkleyEnvironment_.mapFileUseCount.find( walletFilename );
    return (
        berkleyEnvironment_.mapFileUseCount.count(walletFilename) > 0 &&
        ((it != berkleyEnvironment_.mapFileUseCount.end())?  (it->second > 0)  :  false)
    );
}

CCriticalSection& DatabaseWrapper::GetDatabaseLock()
{
    return berkleyEnvironment_.cs_db;
}