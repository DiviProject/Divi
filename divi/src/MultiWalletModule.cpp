#include <MultiWalletModule.h>
#include <MerkleTxConfirmationNumberCalculator.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
#include <wallet.h>
#include <Logging.h>
#include <Settings.h>

MultiWalletModule::MultiWalletModule(
    Settings& settings,
    CTxMemPool& transactionMemoryPool,
    CCriticalSection& mmainCriticalSection,
    const int coinbaseConfirmationsForMaturity
    ): chainStateReference_()
    , settings_(settings)
    , walletIsDisabled_( settings_.GetBoolArg("-disablewallet", false) )
    , walletFilename_( walletIsDisabled_? "" : settings_.GetArg("-wallet", "wallet.dat"))
    , confirmationCalculator_(
        new MerkleTxConfirmationNumberCalculator(
            chainStateReference_->ActiveChain(),
            chainStateReference_->GetBlockMap(),
            coinbaseConfirmationsForMaturity,
            transactionMemoryPool,
            mmainCriticalSection) )
    , walletDbEndpointFactory_(
        new LegacyWalletDatabaseEndpointFactory(walletFilename_,settings_))
    , activeWallet_(
        walletIsDisabled_
        ? nullptr
        : new CWallet(
            *walletDbEndpointFactory_,
            chainStateReference_->ActiveChain(),
            chainStateReference_->GetBlockMap(),
            *confirmationCalculator_))
{
}

MultiWalletModule::~MultiWalletModule()
{
    activeWallet_.reset();
    walletDbEndpointFactory_.reset();
    confirmationCalculator_.reset();
}

const I_MerkleTxConfirmationNumberCalculator& MultiWalletModule::getConfirmationsCalculator() const
{
    return *confirmationCalculator_;
}

const LegacyWalletDatabaseEndpointFactory& MultiWalletModule::getWalletDbEnpointFactory() const
{
    return *walletDbEndpointFactory_;
}
CWallet* MultiWalletModule::getActiveWallet() const
{
    return activeWallet_.get();
}