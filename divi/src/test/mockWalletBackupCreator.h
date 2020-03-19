#ifndef MOCKWALLETBACKUPCREATOR_H
#define MOCKWALLETBACKUPCREATOR_H

#include <i_walletBackupCreator.h>
#include <gmock/gmock.h>


class MockWalletBackupCreator : public I_WalletBackupCreator
{
public:
    MockWalletBackupCreator(){};

    virtual ~MockWalletBackupCreator(){};
    MOCK_METHOD0(BackupWallet, bool());
    MOCK_METHOD1(CheckWalletIntegrity, bool(bool));
};

#endif //MOCKWALLETBACKUPCREATOR_H