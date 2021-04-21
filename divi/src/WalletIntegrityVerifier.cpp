#include <WalletIntegrityVerifier.h>

#include <I_Filesystem.h>
#include <I_DatabaseWrapper.h>
#include "utiltime.h"

WalletIntegrityVerifier::WalletIntegrityVerifier(
    I_FileSystem& fileSystem,
    I_DatabaseWrapper& database
    ): fileSystem_(fileSystem)
    , dbInterface_(database)
    , backupCount_(0u)
{
}

bool WalletIntegrityVerifier::backupDatabaseIfUnavailable(
    const std::string& dataDirectory)
{
    if(!dbInterface_.Open(dataDirectory))
    {
        try
        {
            fileSystem_.rename(
            dataDirectory + "/database",
            dataDirectory + "/database."+
                std::to_string(backupCount_++)+"_"+
                std::to_string(GetTime())+".bak");
        }
        catch(...)
        {
        }

        if(!dbInterface_.Open(dataDirectory))
        {
            return false;
        }
    }
    return true;
}


bool WalletIntegrityVerifier::CheckWalletIntegrity(
    const std::string& dataDirectory,
    const std::string& walletFilename)
{
    if(!backupDatabaseIfUnavailable(dataDirectory)) return false;
    if(fileSystem_.exists(dataDirectory+"/"+walletFilename))
    {
        if(dbInterface_.Verify(walletFilename) == I_DatabaseWrapper::RECOVERY_FAIL)
        {
            return false;
        }
    }
    return true;
}