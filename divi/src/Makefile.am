DIST_SUBDIRS = secp256k1
AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS)


if EMBEDDED_LEVELDB
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/include
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/helpers/memenv
LIBLEVELDB += $(builddir)/leveldb/libleveldb.a
LIBMEMENV  += $(builddir)/leveldb/libmemenv.a

# NOTE: This dependency is not strictly necessary, but without it make may try to build both in parallel, which breaks the LevelDB build system in a race
$(LIBLEVELDB): $(LIBMEMENV)

$(LIBLEVELDB) $(LIBMEMENV):
	@echo "Building LevelDB ..." && $(MAKE) -C $(@D) $(@F) CXX="$(CXX)" \
	  CC="$(CC)" PLATFORM=$(TARGET_OS) AR="$(AR)" $(LEVELDB_TARGET_FLAGS) \
          OPT="$(CXXFLAGS) $(CPPFLAGS)"
endif

BITCOIN_CONFIG_INCLUDES=-I$(builddir)/config
BITCOIN_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

BITCOIN_INCLUDES += -I$(srcdir)/secp256k1/include

LIBBITCOIN_SERVER=libbitcoin_server.a
LIBBITCOIN_WALLET=libbitcoin_wallet.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBBITCOIN_UNIVALUE=univalue/libbitcoin_univalue.a
LIBBITCOIN_ZEROCOIN=libzerocoin/libbitcoin_zerocoin.a
LIBBITCOINQT=qt/libbitcoinqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la
LIBGMOCK=GMock/libgmock.a
GMOCK=GMock/gmock-all.o
GTEST=GMock/gtest-all.o
GMOCK_INCLUDE=GMock/googlemock/include
GTEST_INCLUDE=GMock/googletest/include
GMOCK_ALL=GMock/googlemock/src/gmock-all.cc
GTEST_ALL=GMock/googletest/src/gtest-all.cc

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a
endif

$(GMOCK): $(wildcard ${GMOCK_INCLUDE}/*) $(wildcard ${GTEST_INCLUDE}/*) $(GMOCK_ALL)
	$(CC) -isystem ${GMOCK_INCLUDE} -IGMock/googlemock/ -isystem ${GTEST_INCLUDE} -IGMock/googletest -pthread -c ${GMOCK_ALL} -o $@

$(GTEST): $(wildcard ${GMOCK_INCLUDE}/*) $(wildcard ${GTEST_INCLUDE}/*) $(GTEST_ALL)
	$(CC) -isystem ${GMOCK_INCLUDE} -IGMock/googlemock/ -isystem ${GTEST_INCLUDE} -IGMock/googletest -pthread -c ${GTEST_ALL} -o $@

$(LIBGMOCK): $(GTEST) $(GMOCK)
	ar -rv $@ $^

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES = \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UNIVALUE) \
  $(LIBBITCOIN_ZEROCOIN) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI)

if ENABLE_WALLET
BITCOIN_INCLUDES += $(BDB_CPPFLAGS)
EXTRA_LIBRARIES += $(LIBBITCOIN_WALLET)
endif

if ENABLE_ZMQ
EXTRA_LIBRARIES += $(LIBBITCOIN_ZMQ)
endif

if BUILD_BITCOIN_LIBS
lib_LTLIBRARIES = libbitcoinconsensus.la
LIBBITCOIN_CONSENSUS=libbitcoinconsensus.la
else
LIBBITCOIN_CONSENSUS=
endif

bin_PROGRAMS =
TESTS =

if BUILD_BITCOIND
  bin_PROGRAMS += divid
endif

if BUILD_BITCOIN_UTILS
  bin_PROGRAMS += divi-cli divi-tx
endif

.PHONY: FORCE
# divi core #
BITCOIN_CORE_H = \
  activemasternode.h \
  MasternodeHelpers.h \
  addrman.h \
  alert.h \
  allocators.h \
  addressindex.h \
  amount.h \
  base58.h \
  base58data.h \
  base58address.h \
  BIP9Deployment.h \
  BIP9ActivationManager.h \
  BIP9ActivationFeatureContainer.h \
  BlockIndexLotteryUpdater.h \
  CachedBIP9ActivationStateTracker.h \
  bip38.h \
  bip39.h \
  bip39_english.h \
  bloom.h \
  blockmap.h\
  BlockSubsidyProvider.h \
  BlockRewards.h \
  BlockSigning.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkpoint_data.h\
  checkqueue.h \
  clientversion.h \
  coincontrol.h \
  coins.h \
  compat.h \
  destination.h \
  compat/endian.h \
  compat/byteswap.h \
  compat/sanity.h \
  compressor.h \
  primitives/block.h \
  primitives/transaction.h \
  core_io.h \
  crypter.h \
  obfuscation.h \
  MasternodeModule.h \
  MasternodeBroadcastFactory.h \
  RpcMasternodeFeatures.h \
  db.h \
  dbenv.h \
  DatabaseWrapper.h\
  eccryptoverify.h \
  ecwrapper.h \
  FeeRate.h \
  flat-database.h \
  FilteredBoostFileSystem.h \
  ForkActivation.h \
  Settings.h \
  hash.h \
  hdchain.h \
  init.h \
  LicenseAndInfo.h \
  IndexDatabaseUpdates.h \
  BlockTransactionChecker.h \
  FeeAndPriorityCalculator.h \
  I_Filesystem.h \
  I_WalletBackupCreator.h \
  I_DatabaseWrapper.h \
  I_SuperblockHeightValidator.h \
  I_BlockIncentivesPopulator.h \
  I_BlockSubsidyProvider.h \
  I_BIP9ActivationStateTracker.h \
  I_BIP9ActivationTrackerFactory.h \
  I_SuperblockHeightValidator.h \
  I_BlockSubsidyProvider.h \
  I_PoSStakeModifierService.h \
  I_ProofOfStakeCalculator.h \
  I_PeerSyncQueryService.h \
  I_PeerBlockNotifyService.h \
  I_Clock.h \
  I_BlockchainSyncQueryService.h \
  I_BlockDataReader.h \
  ProofOfStakeCalculator.h \
  uiMessenger.h \
  kernel.h \
  I_ProofOfStakeGenerator.h \
  ProofOfStakeGenerator.h \
  ProofOfStakeModule.h \
  StakeModifierIntervalHelpers.h \
  StakingData.h \
  key.h \
  keystore.h \
  LegacyBlockSubsidies.h \
  LegacyPoSStakeModifierService.h \
  PoSStakeModifierService.h \
  leveldbwrapper.h \
  limitedmap.h \
  defaultValues.h \
  blockFileInfo.h \
  scriptCheck.h \
  verifyDb.h \
  BlockUndo.h \
  ValidationState.h \
  ActiveChainManager.h \
  IndexDatabaseUpdateCollector.h \
  NodeState.h \
  NodeStateRegistry.h \
  BlocksInFlightRegistry.h \
  NodeSignals.h \
  BlockRejects.h\
  QueuedBlock.h \
  main.h \
  OrphanTransactions.h \
  TransactionOpCounting.h \
  TransactionInputChecker.h \
  UtxoCheckingAndUpdating.h\
  WalletLoggingHelper.h \
  walletdustcombiner.h \
  BlockFileOpener.h \
  BlockDiskAccessor.h \
  TransactionDiskAccessor.h \
  BlockTemplate.h \
  I_BlockFactory.h \
  I_BlockTransactionCollector.h \
  I_SuperblockSubsidyContainer.h \
  I_PoSTransactionCreator.h \
  ExtendedBlockFactory.h \
  BlockFactory.h \
  masternode.h \
  MasternodePing.h \
  MasternodePaymentData.h \
  MasternodePayeeData.h \
  masternode-payments.h \
  LotteryCoinstakes.h \
  LotteryWinnersCalculator.h \
  BlockIncentivesPopulator.h \
  SuperblockSubsidyContainer.h \
  SuperblockHeightValidator.h \
  masternode-tier.h \
  masternode-sync.h \
  MasternodeNetworkMessageManager.h \
  masternodeman.h \
  masternodeconfig.h \
  merkleblock.h \
  merkletx.h \
  miner.h \
  I_CoinMinter.h \
  BlockMemoryPoolTransactionCollector.h \
  CoinMinter.h \
  CoinMintingModule.h \
  PoSTransactionCreator.h \
  PeerNotificationOfMintService.h \
  MonthlyWalletBackupCreator.h \
  MinimumFeeCoinSelectionAlgorithm.h \
  mruset.h \
  netbase.h \
  netfulfilledman.h \
  net.h \
  AddrDB.h \
  Node.h \
  SocketChannel.h \
  I_CommunicationRegistrar.h \
  I_CommunicationChannel.h \
  NodeId.h \
  NodeStats.h \
  NetworkLocalAddressHelpers.h \
  PeerBanningService.h \
  OutputEntry.h \
  noui.h \
  pow.h \
  protocol.h \
  pubkey.h \
  random.h \
  reverse_iterate.h \
  rpcclient.h \
  rpcprotocol.h \
  rpcserver.h \
  script/interpreter.h \
  script/SignatureCheckers.h \
  script/StackManager.h \
  script/script.h \
  script/opcodes.h \
  script/scriptandsigflags.h \
  script/sigcache.h \
  script/sign.h \
  script/StakingVaultScript.h \
  script/standard.h \
  script/script_error.h \
  serialize.h \
  Secp256k1Context.h \
  spork.h \
  sporkdb.h \
  spentindex.h \
  StoredMasternodeBroadcasts.h \
  streams.h \
  sync.h \
  SpentOutputTracker.h \
  ThresholdConditionCache.h \
  threadsafety.h \
  timedata.h \
  timeIntervalConstants.h \
  tinyformat.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  uint256.h \
  undo.h \
  util.h \
  ThreadManagementHelpers.h \
  Logging.h \
  DataDirectory.h \
  utilstrencodings.h \
  utilmoneystr.h \
  utiltime.h \
  NotificationInterface.h \
  versionbits.h \
  version.h \
  I_VaultManagerDatabase.h \
  VaultManager.h \
  Output.h \
  I_SignatureSizeEstimator.h \
  SignatureSizeEstimator.h \
  I_CoinSelectionAlgorithm.h \
  CoinControlSelectionAlgorithm.h \
  StochasticSubsetSelectionAlgorithm.h \
  wallet.h \
  WalletTx.h \
  WalletTransactionRecord.h \
  StakableCoin.h \
  keypool.h \
  reservekey.h \
  WalletBackupCreator.h \
  WalletBackupFeatureContainer.h \
  WalletIntegrityVerifier.h \
  wallet_ismine.h \
  walletdb.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h \
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  compat/sanity.h

JSON_H = \
  json/json_spirit.h \
  json/json_spirit_error_position.h \
  json/json_spirit_reader.h \
  json/json_spirit_reader_template.h \
  json/json_spirit_stream_reader.h \
  json/json_spirit_utils.h \
  json/json_spirit_value.h \
  json/json_spirit_writer.h \
  json/json_spirit_writer_template.h

obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh $(abs_top_builddir)/src/obj/build.h \
	  $(abs_top_srcdir)
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between divid and divi-qt
libbitcoin_server_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_SOURCES = \
  addrman.cpp \
  alert.cpp \
  BlockRewards.cpp \
  BIP9Deployment.cpp \
  BIP9ActivationManager.cpp \
  BIP9ActivationFeatureContainer.cpp \
  BlockRewards.cpp \
  BlockSigning.cpp \
  BlockIndexLotteryUpdater.cpp \
  CachedBIP9ActivationStateTracker.cpp \
  bloom.cpp \
  chain.cpp \
  checkpoints.cpp \
  FilteredBoostFileSystem.cpp \
  init.cpp \
  IndexDatabaseUpdates.cpp \
  BlockTransactionChecker.cpp \
  FeeAndPriorityCalculator.cpp \
  ForkActivation.cpp \
  uiMessenger.cpp \
  leveldbwrapper.cpp \
  blockFileInfo.cpp \
  scriptCheck.cpp \
  verifyDb.cpp \
  BlockUndo.cpp \
  ValidationState.cpp \
  TransactionOpCounting.cpp \
  TransactionInputChecker.cpp \
  UtxoCheckingAndUpdating.cpp\
  ActiveChainManager.cpp \
  IndexDatabaseUpdateCollector.cpp \
  NodeState.cpp \
  BlocksInFlightRegistry.cpp \
  NodeStateRegistry.cpp \
  main.cpp \
  OrphanTransactions.cpp \
  WalletLoggingHelper.cpp \
  walletdustcombiner.cpp \
  BlockFactory.cpp \
  ExtendedBlockFactory.cpp \
  BlockFileOpener.cpp \
  BlockDiskAccessor.cpp \
  TransactionDiskAccessor.cpp \
  merkleblock.cpp \
  merkletx.cpp \
  CoinMinter.cpp \
  CoinMintingModule.cpp \
  PoSTransactionCreator.cpp \
  kernel.cpp \
  ProofOfStakeGenerator.cpp \
  ProofOfStakeModule.cpp \
  ProofOfStakeCalculator.cpp \
  LegacyPoSStakeModifierService.cpp \
  Logging-server.cpp \
  PoSStakeModifierService.cpp \
  PeerNotificationOfMintService.cpp \
  miner.cpp \
  MasternodeHelpers.cpp \
  MasternodeModule.cpp \
  BlockMemoryPoolTransactionCollector.cpp \
  MonthlyWalletBackupCreator.cpp \
  net.cpp \
  AddrDB.cpp \
  Node.cpp \
  SocketChannel.cpp \
  NodeStats.cpp \
  NetworkLocalAddressHelpers.cpp \
  PeerBanningService.cpp \
  netfulfilledman.cpp \
  noui.cpp \
  pow.cpp \
  rest.cpp \
  rpcblockchain.cpp \
  rpclottery.cpp \
  rpcmasternode.cpp \
  RpcMasternodeFeatures.cpp \
  rpcmining.cpp \
  rpcmisc.cpp \
  rpcnet.cpp \
  rpcrawtransaction.cpp \
  rpcserver.cpp \
  script/sigcache.cpp \
  sporkdb.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  NotificationInterface.cpp \
  version.cpp \
  versionbits.cpp \
  WalletBackupCreator.cpp \
  WalletBackupFeatureContainer.cpp \
  WalletIntegrityVerifier.cpp \
  $(JSON_H) \
  $(BITCOIN_CORE_H)

if ENABLE_ZMQ
libbitcoin_zmq_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif

# wallet: shared between divid and divi-qt, but only linked
# when wallet enabled

libbitcoin_wallet_a_CFLAGS = -fPIC
libbitcoin_wallet_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_wallet_a_SOURCES = \
  activemasternode.cpp \
  bip38.cpp \
  BlockSubsidyProvider.cpp \
  obfuscation.cpp \
  MasternodeBroadcastFactory.cpp \
  uiMessenger.cpp \
  db.cpp \
  dbenv.cpp \
  DatabaseWrapper.cpp \
  crypto/aes.cpp \
  crypter.cpp \
  keypool.cpp \
  LegacyBlockSubsidies.cpp \
  Logging-wallet.cpp \
  masternode.cpp \
  MasternodePing.cpp \
  MasternodePaymentData.cpp \
  MasternodePayeeData.cpp \
  masternode-payments.cpp \
  LotteryWinnersCalculator.cpp \
  LotteryCoinstakes.cpp \
  BlockIncentivesPopulator.cpp \
  SuperblockSubsidyContainer.cpp \
  SuperblockHeightValidator.cpp \
  SpentOutputTracker.cpp \
  StoredMasternodeBroadcasts.cpp \
  masternode-sync.cpp \
  masternodeconfig.cpp \
  MasternodeNetworkMessageManager.cpp \
  NodeState.cpp \
  BlocksInFlightRegistry.cpp \
  NodeStateRegistry.cpp \
  masternodeman.cpp \
  netfulfilledman.cpp \
  reservekey.cpp \
  rpcdump.cpp \
  rpcwallet.cpp \
  VaultManager.cpp \
  SignatureSizeEstimator.cpp \
  CoinControlSelectionAlgorithm.cpp \
  StochasticSubsetSelectionAlgorithm.cpp \
  MinimumFeeCoinSelectionAlgorithm.cpp \
  wallet.cpp \
  Output.cpp \
  WalletTx.cpp \
  WalletTransactionRecord.cpp \
  merkletx.cpp \
  wallet_ismine.cpp \
  walletdb.cpp \
  $(BITCOIN_CORE_H)

# crypto primitives library
crypto_libbitcoin_crypto_a_CFLAGS = -fPIC
crypto_libbitcoin_crypto_a_CPPFLAGS = $(BITCOIN_CONFIG_INCLUDES)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/ctaes/ctaes.h \
  crypto/ctaes/ctaes.c \
  crypto/sha1.cpp \
  crypto/sha256.cpp \
  crypto/sha512.cpp \
  crypto/hmac_sha256.cpp \
  crypto/rfc6979_hmac_sha256.cpp \
  crypto/hmac_sha512.cpp \
  crypto/scrypt.cpp \
  crypto/ripemd160.cpp \
  crypto/aes_helper.c \
  crypto/blake.c \
  crypto/bmw.c \
  crypto/groestl.c \
  crypto/jh.c \
  crypto/keccak.c \
  crypto/skein.c \
  crypto/common.h \
  crypto/sha256.h \
  crypto/sha512.h \
  crypto/hmac_sha256.h \
  crypto/rfc6979_hmac_sha256.h \
  crypto/hmac_sha512.h \
  crypto/scrypt.h \
  crypto/sha1.h \
  crypto/ripemd160.h \
  crypto/sph_blake.h \
  crypto/sph_bmw.h \
  crypto/sph_groestl.h \
  crypto/sph_jh.h \
  crypto/sph_keccak.h \
  crypto/sph_skein.h \
  crypto/sph_types.h \
  crypto/sha512.cpp \
  crypto/sha512.h

# univalue JSON library
univalue_libbitcoin_univalue_a_SOURCES = \
  univalue/univalue.cpp \
  univalue/univalue_read.cpp \
  univalue/univalue_write.cpp \
  univalue/univalue_escapes.h \
  univalue/univalue.h

# libzerocoin library
libzerocoin_libbitcoin_zerocoin_a_SOURCES = \
  libzerocoin/bignum.h \
  libzerocoin/Denominations.h \
  libzerocoin/Denominations.cpp

# common: shared between divid, and divi-qt and non-server tools
libbitcoin_common_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_common_a_SOURCES = \
  allocators.cpp \
  FeeRate.cpp \
  base58.cpp \
  base58data.cpp \
  base58address.cpp \
  bip38.cpp \
  bip39.cpp \
  chainparams.cpp \
  coins.cpp \
  NodeState.cpp \
  BlocksInFlightRegistry.cpp \
  NodeStateRegistry.cpp \
  FeeAndPriorityCalculator.cpp \
  compressor.cpp \
  destination.cpp \
  primitives/block.cpp \
  primitives/transaction.cpp \
  core_io.cpp \
  eccryptoverify.cpp \
  ecwrapper.cpp \
  flat-database.cpp \
  hash.cpp \
  hdchain.cpp \
  key.cpp \
  keystore.cpp \
  Logging-common.cpp \
  netbase.cpp \
  protocol.cpp \
  pubkey.cpp \
  script/interpreter.cpp \
  script/SignatureCheckers.cpp \
  script/StackManager.cpp \
  script/opcodes.cpp \
  script/script.cpp \
  script/StakingVaultScript.cpp \
  script/sign.cpp \
  script/standard.cpp \
  script/script_error.cpp \
  Secp256k1Context.cpp \
  spork.cpp \
  sporkdb.cpp \
  $(BITCOIN_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_util_a_SOURCES = \
  allocators.cpp \
  compat/strnlen.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  Settings.cpp \
  random.cpp \
  rpcprotocol.cpp \
  sync.cpp \
  uint256.cpp \
  util.cpp \
  ThreadManagementHelpers.cpp \
  Logging.cpp \
  DataDirectory.cpp \
  utilstrencodings.cpp \
  utilmoneystr.cpp \
  utiltime.cpp \
  version-util.cpp \
  LicenseAndInfo.cpp \
  $(BITCOIN_CORE_H)

if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
libbitcoin_util_a_SOURCES += compat/glibcxx_compat.cpp
endif

# cli: shared between divi-cli and divi-qt
libbitcoin_cli_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_cli_a_SOURCES = \
  rpcclient.cpp \
  $(BITCOIN_CORE_H)

nodist_libbitcoin_util_a_SOURCES = $(srcdir)/obj/build.h
#

# bitcoind binary #
divid_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UNIVALUE) \
  $(LIBBITCOIN_ZEROCOIN) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

if ENABLE_ZMQ
divid_LDADD += $(LIBBITCOIN_ZMQ) $(ZMQ_LIBS)
endif

if ENABLE_WALLET
divid_LDADD += $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_CRYPTO)
endif
divid_SOURCES = divid.cpp
#

if TARGET_WINDOWS
divid_SOURCES += divid-res.rc
endif

divid_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS)
divid_CPPFLAGS = $(BITCOIN_INCLUDES)
divid_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)


# divi-cli binary #
divi_cli_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(BOOST_LIBS) \
  $(SSL_LIBS) \
  $(CRYPTO_LIBS) \
  $(EVENT_LIBS)

divi_cli_SOURCES = \
  divi-cli.cpp

divi_cli_CPPFLAGS = $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
#

# divi-tx binary #
divi_tx_LDADD = \
  $(LIBBITCOIN_UNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_ZEROCOIN) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBSECP256K1) \
  $(BOOST_LIBS) \
  $(CRYPTO_LIBS)

divi_tx_SOURCES = divi-tx.cpp
divi_tx_CPPFLAGS = $(BITCOIN_INCLUDES)
#
divi_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
divi_cli_SOURCES += divi-cli-res.rc
endif
divi_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if BUILD_BITCOIN_LIBS
include_HEADERS = script/bitcoinconsensus.h
libbitcoinconsensus_la_SOURCES = \
  allocators.cpp \
  primitives/transaction.cpp \
  crypto/hmac_sha512.cpp \
  crypto/scrypt.cpp \
  crypto/sha1.cpp \
  crypto/sha256.cpp \
  crypto/sha512.cpp \
  crypto/ripemd160.cpp \
  eccryptoverify.cpp \
  ecwrapper.cpp \
  hash.cpp \
  pubkey.cpp \
  script/script.cpp \
  script/interpreter.cpp \
  script/StackManager.cpp \
  script/opcodes.cpp \
  script/bitcoinconsensus.cpp \
  uint256.cpp \
  utilstrencodings.cpp

if GLIBC_BACK_COMPAT
libbitcoinconsensus_la_SOURCES += compat/glibc_compat.cpp
libbitcoinconsensus_la_SOURCES += compat/glibcxx_compat.cpp
endif

libbitcoinconsensus_la_LDFLAGS = -no-undefined $(RELDFLAGS)
libbitcoinconsensus_la_LIBADD = $(CRYPTO_LIBS) $(LIBSECP256K1) $(BOOST_LIBS)
libbitcoinconsensus_la_CPPFLAGS = $(CRYPTO_CFLAGS) -I$(builddir)/obj -DBUILD_BITCOIN_INTERNAL
endif

CLEANFILES = leveldb/libleveldb.a leveldb/libmemenv.a
CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = leveldb

clean-local:
	-$(MAKE) -C leveldb clean
	-$(MAKE) -C secp256k1 clean
	rm -f leveldb/*/*.gcno leveldb/helpers/memenv/*.gcno
	-rm -f config.h

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(QT_INCLUDES) $(CXXFLAGS)  -c -o $@ $<

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(abspath $(<D) $<)

if ENABLE_TESTS
include Makefile.test.include
endif
