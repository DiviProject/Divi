#ifndef I_WALLETBACKUPCREATOR_H
#define I_WALLETBACKUPCREATOR_H
class I_WalletBackupCreator
{
public:
    virtual ~I_WalletBackupCreator(){}
    virtual bool BackupWallet() = 0;
    virtual bool CheckWalletIntegrity(bool resync) = 0;
};

#endif //I_WALLETBACKUPCREATOR_H 