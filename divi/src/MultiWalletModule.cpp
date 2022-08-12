#include <MultiWalletModule.h>
#include <MerkleTxConfirmationNumberCalculator.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
#include <wallet.h>
#include <Logging.h>
#include <Settings.h>

DatabaseBackedWallet::DatabaseBackedWallet(
    const std::string walletFilename,
    Settings& settings,
    const CChain& activeChain,
    const BlockMap& blockIndicesByHash,
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator
    ): walletDbEndpointFactory_(new LegacyWalletDatabaseEndpointFactory(walletFilename,settings))
    , wallet_(
        new CWallet(
            *walletDbEndpointFactory_,
            activeChain,
            blockIndicesByHash,
            confirmationsCalculator))
{
}

DatabaseBackedWallet::~DatabaseBackedWallet()
{
    wallet_.reset();
    walletDbEndpointFactory_.reset();
}

DatabaseBackedWallet::DatabaseBackedWallet(
    DatabaseBackedWallet&& other
    ): walletDbEndpointFactory_()
    , wallet_()
{
    walletDbEndpointFactory_ = std::move(other.walletDbEndpointFactory_);
    wallet_ = std::move(other.wallet_);
}

MultiWalletModule::MultiWalletModule(
    Settings& settings,
    CTxMemPool& transactionMemoryPool,
    CCriticalSection& mmainCriticalSection,
    const int coinbaseConfirmationsForMaturity
    ): chainStateReference_()
    , settings_(settings)
    , walletIsDisabled_( settings_.GetBoolArg("-disablewallet", false) )
    , confirmationCalculator_(
        new MerkleTxConfirmationNumberCalculator(
            chainStateReference_->ActiveChain(),
            chainStateReference_->GetBlockMap(),
            coinbaseConfirmationsForMaturity,
            transactionMemoryPool,
            mmainCriticalSection) )
    , backedWalletsByName_()
    , walletDbEndpointFactoryByName_()
    , walletsByName_()
    , activeWallet_(nullptr)
{
}

MultiWalletModule::~MultiWalletModule()
{
    activeWallet_ = nullptr;
    walletsByName_.clear();
    walletDbEndpointFactoryByName_.clear();
    backedWalletsByName_.clear();
    confirmationCalculator_.reset();
}

bool MultiWalletModule::loadWallet(const std::string walletFilename)
{
    if(walletIsDisabled_) return false;
    if(walletDbEndpointFactoryByName_.find(walletFilename) != walletDbEndpointFactoryByName_.end()) return true;

    std::unique_ptr<LegacyWalletDatabaseEndpointFactory> dbEndpointFactory(new LegacyWalletDatabaseEndpointFactory(walletFilename,settings_));
    walletDbEndpointFactoryByName_[walletFilename] = std::move(dbEndpointFactory);

    std::unique_ptr<CWallet> wallet(
        new CWallet(
            *walletDbEndpointFactoryByName_[walletFilename],
            chainStateReference_->ActiveChain(),
            chainStateReference_->GetBlockMap(),
            *confirmationCalculator_));
    walletsByName_[walletFilename] = std::move(wallet);

    return true;
}
bool MultiWalletModule::setActiveWallet(const std::string walletFilename)
{
    if(walletIsDisabled_ || walletDbEndpointFactoryByName_.find(walletFilename) == walletDbEndpointFactoryByName_.end()) return false;

    activeWalletDbEndpoint_ = walletDbEndpointFactoryByName_.find(walletFilename)->second.get();
    activeWallet_ = walletsByName_.find(walletFilename)->second.get();
    return true;
}

const I_MerkleTxConfirmationNumberCalculator& MultiWalletModule::getConfirmationsCalculator() const
{
    return *confirmationCalculator_;
}

const LegacyWalletDatabaseEndpointFactory& MultiWalletModule::getWalletDbEnpointFactory() const
{
    return *activeWalletDbEndpoint_;
}
CWallet* MultiWalletModule::getActiveWallet() const
{
    return activeWallet_;
}