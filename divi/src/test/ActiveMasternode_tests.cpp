#include <activemasternode.h>
#include <test_only.h>
#include <memory>
#include <cstring>

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


BOOST_AUTO_TEST_CASE(willNotAttemptToResetAlreadyStartedMasternode)
{
    {
        activeMasternode_->status = ACTIVE_MASTERNODE_STARTED;
        CTxIn txIn;
        CService service;
        BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(txIn, service));
    }
    {
        activeMasternode_->status = ACTIVE_MASTERNODE_STARTED;
        CTxIn txIn;
        CService service;
        AddDummyConfiguration(txIn, service);
        BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(txIn, service));
    }
}


BOOST_AUTO_TEST_CASE(willNotEnableMasternodeOnEmptyConfigurations)
{
    CTxIn wrongTransaction;
    CService service;
    BOOST_CHECK(! activeMasternode_->EnableHotColdMasterNode(wrongTransaction, service));
}

BOOST_AUTO_TEST_SUITE_END()