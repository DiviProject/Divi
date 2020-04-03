#ifndef I_WALLETBACKUPCREATOR_H
#define I_WALLETBACKUPCREATOR_H

#include <string>
class I_WalletBackupCreator
{
public:
    virtual ~I_WalletBackupCreator(){}
    virtual bool BackupWallet() = 0;
    virtual std::string GetBackupSubfolderDirectory() const = 0;
};

#endif //I_WALLETBACKUPCREATOR_H 