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

bool WalletIntegrityVerifier::CheckWalletIntegrity()
{
    return true;
}