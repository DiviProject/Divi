#include <activemasternode.h>
#include <masternodeconfig.h>
#include <test_only.h>
#include <memory>
#include <cstring>
#include <random.h>
#include "uint256.h"
#include <key.h>
#include <base58.h>

class ActiveMasternodeTestFixture
{
public:

    std::shared_ptr<CMasternodeConfig> configurations_;
    bool masternodesEnabled_;
    std::shared_ptr<CActiveMasternode> activeMasternode_;

    ActiveMasternodeTestFixture(
        ): configurations_(new CMasternodeConfig)
        , masternodesEnabled_(true)
        , activeMasternode_(new CActiveMasternode(*configurations_, masternodesEnabled_))
    {
    }

    void AddDummyConfiguration(CTxIn txIn, CService service)
    {
        configurations_->add("dummy configuration", service.ToString(), "", txIn.prevout.hash.ToString(), std::to_string(txIn.prevout.n));
    }
};


BOOST_FIXTURE_TEST_SUITE(ActiveMasternodeTests, ActiveMasternodeTestFixture)


BOOST_AUTO_TEST_CASE(willNotAttemptToResetAlreadyStartedMasternodeIfConfigurationIsUnknown)
{
    activeMasternode_->status = ACTIVE_MASTERNODE_STARTED;
    CTxIn txIn;
    CService service;
    BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(txIn, service));
}

BOOST_AUTO_TEST_CASE(willNotAttemptToResetAlreadyStartedMasternodeIfConfigurationIsKnown)
{
    activeMasternode_->status = ACTIVE_MASTERNODE_STARTED;
    CTxIn txIn;
    CService service;
    AddDummyConfiguration(txIn, service);
    BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(txIn, service));
}

BOOST_AUTO_TEST_CASE(willNotEnableMasternodeOnEmptyConfigurations)
{
    CTxIn wrongTransaction;
    CService service;
    BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(wrongTransaction, service));
    BOOST_CHECK(activeMasternode_->status != ACTIVE_MASTERNODE_STARTED);
}

BOOST_AUTO_TEST_CASE(willEnableMasternodeOnMatchingUTXO)
{
    uint256 dummyHash = GetRandHash();
    uint32_t out = 0;
    CTxIn validTxIn (dummyHash, out);
    CService service;

    AddDummyConfiguration(validTxIn, service);
    BOOST_CHECK(activeMasternode_->EnableHotColdMasterNode(validTxIn, service));
    BOOST_CHECK(activeMasternode_->status == ACTIVE_MASTERNODE_STARTED);
}

BOOST_AUTO_TEST_CASE(willNotEnableMasternodeOnMismatchedUTXO)
{
    uint32_t out = 0;

    uint256 correctDummyHash = GetRandHash();
    CTxIn validTxIn (correctDummyHash, out);

    uint256 wrongDummyHash = GetRandHash();
    CTxIn wrongTxIn (wrongDummyHash, out);


    CService service;

    AddDummyConfiguration(validTxIn, service);
    BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(wrongTxIn, service));
    BOOST_CHECK(activeMasternode_->status != ACTIVE_MASTERNODE_STARTED);
}

BOOST_AUTO_TEST_CASE(willSetMatchingPubkeyForPrivateKey)
{
    CKey privateKey;
    privateKey.MakeNewKey(true);
    CPubKey expectedPubkey = privateKey.GetPubKey();
    std::string privateKeyAsString = CBitcoinSecret(privateKey).ToString();
    BOOST_CHECK(activeMasternode_->SetMasternodeKey(privateKeyAsString));
    BOOST_CHECK(activeMasternode_->pubKeyMasternode == expectedPubkey);
}

BOOST_AUTO_TEST_SUITE_END()