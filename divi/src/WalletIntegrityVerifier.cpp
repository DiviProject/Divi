#include <WalletIntegrityVerifier.h>

#include <I_Filesystem.h>
#include <I_DatabaseWrapper.h>
#include "utiltime.h"

WalletIntegrityVerifier::WalletIntegrityVerifier(
    const std::string& dataDirectory,
    I_FileSystem& fileSystem,
    I_DatabaseWrapper& database
    ): dataDirectory_(dataDirectory)
    , fileSystem_(fileSystem)
    , dbInterface_(database)
    , backupCount_(0u)
{
}

bool WalletIntegrityVerifier::backupDatabaseIfUnavailable()
{
    if(!dbInterface_.Open())
    {
        try
        {
            fileSystem_.rename(
            dataDirectory_ + "/database",
            dataDirectory_ + "/database."+
                std::to_string(backupCount_++)+"_"+
                std::to_string(GetTime())+".bak");
        }
        catch(...)
        {
        }

        if(!dbInterface_.Open())
        {
            return false;
        }
    }
    return true;
}


bool WalletIntegrityVerifier::CheckWalletIntegrity(const std::string& walletFilename)
{
    if(!backupDatabaseIfUnavailable()) return false;
    if(fileSystem_.exists(dataDirectory_+"/"+walletFilename))
    {
        if(dbInterface_.Verify(walletFilename) == I_DatabaseWrapper::RECOVERY_FAIL)
        {
            return false;
        }
    }
    return true;
}