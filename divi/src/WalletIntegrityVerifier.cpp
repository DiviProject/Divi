#include <WalletIntegrityVerifier.h>

#include <i_filesystem.h>
#include <i_databaseWrapper.h>

WalletIntegrityVerifier::WalletIntegrityVerifier(
    I_FileSystem& fileSystem,
    I_DatabaseWrapper& database
    ): fileSystem_(fileSystem)
    , dbInterface_(database)
{
}

bool WalletIntegrityVerifier::CheckWalletIntegrity(
    const std::string& dataDirectory,
    const std::string& walletFilename)
{
    if(!dbInterface_.Open(dataDirectory))
    {
        try
        {
            fileSystem_.rename(
            dataDirectory + "/database",
            dataDirectory + "/database.bak");
        }
        catch(...)
        {
        }
        
        return false;
    }
    return true;
}