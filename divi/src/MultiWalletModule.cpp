#include <MultiWalletModule.h>
#include <MerkleTxConfirmationNumberCalculator.h>
#include <LegacyWalletDatabaseEndpointFactory.h>
#include <wallet.h>
#include <Logging.h>
#include <Settings.h>
#include <sync.h>
#include <dbenv.h>
#include <ChainstateManager.h>

DatabaseBackedWallet::DatabaseBackedWallet(
    const std::string walletFilename,
    Settings& settings,
    const CChain& activeChain,
    const BlockMap& blockIndicesByHash,
    const I_MerkleTxConfirmationNumberCalculator& confirmationsCalculator,
    CCriticalSection* underlyingWalletCriticalSection
    ): walletFilename_(walletFilename)
    , underlyingWalletCriticalSection_( (!underlyingWalletCriticalSection)?new CCriticalSection() : underlyingWalletCriticalSection )
    , walletDbEndpointFactory_(
        new LegacyWalletDatabaseEndpointFactory(walletFilename,settings))
    , wallet_(
        new CWallet(
            *underlyingWalletCriticalSection_,
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
    underlyingWalletCriticalSection_.reset();
}

DatabaseBackedWallet::DatabaseBackedWallet(
    DatabaseBackedWallet&& other
    ): walletFilename_()
    , underlyingWalletCriticalSection_()
    , walletDbEndpointFactory_()
    , wallet_()
{
    walletFilename_ = std::move(other.walletFilename_);
    underlyingWalletCriticalSection_ = std::move(other.underlyingWalletCriticalSection_);
    walletDbEndpointFactory_ = std::move(other.walletDbEndpointFactory_);
    wallet_ = std::move(other.wallet_);
}

MultiWalletModule::MultiWalletModule(
    const ChainstateManager& chainstate,
    Settings& settings,
    CTxMemPool& transactionMemoryPool,
    CCriticalSection& mmainCriticalSection,
    const int coinbaseConfirmationsForMaturity
    ): chainstate_(chainstate)
    , settings_(settings)
    , walletIsDisabled_( settings_.GetBoolArg("-disablewallet", false) )
    , confirmationCalculator_(
        new MerkleTxConfirmationNumberCalculator(
            chainstate_.ActiveChain(),
            chainstate_.GetBlockMap(),
            coinbaseConfirmationsForMaturity,
            transactionMemoryPool,
            mmainCriticalSection) )
    , backedWalletsByName_()
    , activeWallet_(nullptr)
{
}

MultiWalletModule::~MultiWalletModule()
{
    if(activeWallet_) BerkleyDBEnvWrapper().Flush(true);
    activeWallet_ = nullptr;
    backedWalletsByName_.clear();
    confirmationCalculator_.reset();
}

bool MultiWalletModule::reloadActiveWallet()
{
    if(walletIsDisabled_) return false;
    if(!activeWallet_) return true;
    std::string activeWalletFilename = activeWallet_->walletFilename_;
    BerkleyDBEnvWrapper().Flush(false);
    activeWallet_->wallet_.reset();
    activeWallet_->walletDbEndpointFactory_.reset();

    std::unique_ptr<DatabaseBackedWallet> dbBackedWallet(
        new DatabaseBackedWallet(
            activeWalletFilename,
            settings_,
            chainstate_.ActiveChain(),
            chainstate_.GetBlockMap(),
            *confirmationCalculator_,
            activeWallet_->underlyingWalletCriticalSection_.release() ));

    activeWallet_ = nullptr;
    backedWalletsByName_.erase(activeWalletFilename);
    backedWalletsByName_.emplace(activeWalletFilename, std::move(dbBackedWallet) );
    activeWallet_ = backedWalletsByName_.find(activeWalletFilename)->second.get();
    return true;
}

bool MultiWalletModule::loadWallet(const std::string walletFilename)
{
    if(walletIsDisabled_) return false;
    if(backedWalletsByName_.find(walletFilename) != backedWalletsByName_.end()) return true;

    std::unique_ptr<DatabaseBackedWallet> dbBackedWallet(
        new DatabaseBackedWallet(
            walletFilename,
            settings_,
            chainstate_.ActiveChain(),
            chainstate_.GetBlockMap(),
            *confirmationCalculator_,
            nullptr));
    backedWalletsByName_.emplace(walletFilename, std::move(dbBackedWallet) );
    return true;
}
bool MultiWalletModule::setActiveWallet(const std::string walletFilename)
{
    if(walletIsDisabled_ || backedWalletsByName_.find(walletFilename) == backedWalletsByName_.end()) return false;

    activeWallet_ = backedWalletsByName_.find(walletFilename)->second.get();
    return true;
}

const I_MerkleTxConfirmationNumberCalculator& MultiWalletModule::getConfirmationsCalculator() const
{
    return *confirmationCalculator_;
}

const LegacyWalletDatabaseEndpointFactory& MultiWalletModule::getWalletDbEnpointFactory() const
{
    assert(activeWallet_);
    return *(activeWallet_->walletDbEndpointFactory_);
}
CWallet* MultiWalletModule::getActiveWallet() const
{
    return activeWallet_? activeWallet_->wallet_.get() : nullptr;
}

std::string MultiWalletModule::getActiveWalletName() const
{
    return activeWallet_? activeWallet_->walletFilename_ : "";
}