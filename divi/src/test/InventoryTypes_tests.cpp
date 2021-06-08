#include <test_only.h>
#include <protocol.h>
#include <random.h>

BOOST_AUTO_TEST_SUITE(InventoryTypeTests)

BOOST_AUTO_TEST_CASE(willCheckInventoryTypesAreKnown)
{
    for(int inventoryId = MSG_TX; inventoryId <= MSG_MASTERNODE_PING; ++inventoryId)
    {
        CInv inv(inventoryId,0);
        BOOST_CHECK_MESSAGE(inv.IsKnownType(),"Inventory is of unknown type\n");
    }
}
BOOST_AUTO_TEST_CASE(willCheckInventoryIdsAreWithinRange)
{
    {
        CInv inv(0,0);
        BOOST_CHECK_MESSAGE(!inv.IsKnownType(),"Zero is an invalid inventory id\n");
    }
    {
        CInv inv(MSG_MASTERNODE_PING+1,0);
        BOOST_CHECK_MESSAGE(!inv.IsKnownType(),"Invalid inventory id being treated as known\n");
    }
}
BOOST_AUTO_TEST_CASE(willCheckInventoryCommandsCanBeConvertedToMatchingTypes)
{
    for(int inventoryId = MSG_TX; inventoryId <= MSG_MASTERNODE_PING; ++inventoryId)
    {
        CInv inv(inventoryId,0);
        CInv copiedInventory(inv.GetCommand(),0);
        BOOST_CHECK_MESSAGE(inv.type==copiedInventory.type, "Inventory type does not match inventory command");
    }
    {
        CInv inv(0,0);
        CInv copiedInventory(inv.GetCommand(),0);
        BOOST_CHECK_MESSAGE(inv.type==copiedInventory.type, "Inventory type does not match inventory command");
    }
    {
        CInv inv(MSG_MASTERNODE_PING+1,0);
        CInv copiedInventory(inv.GetCommand(),0);
        BOOST_CHECK_MESSAGE(inv.type==copiedInventory.type, "Inventory type for invalid object was copied into a valid type");
        BOOST_CHECK_MESSAGE(copiedInventory.type == 0, "Erroneous inventory object has been assigned valid type");
    }
}
BOOST_AUTO_TEST_CASE(willCheckForPositiveInventoryIds)
{
    int badInventoryId = GetRandInt(100)-100;
    CInv inv(badInventoryId,0);
    BOOST_CHECK_MESSAGE(std::string(inv.GetCommand())==std::string("ERROR"),"Bad inventory id was used");
}

BOOST_AUTO_TEST_SUITE_END()