#include <AddressBookManager.h>


AddressBookManager::AddressBookManager(): mapAddressBook(), destinationByLabel_()
{
}

const AddressBookManager::LastDestinationByLabel& AddressBookManager::getLastDestinationByLabel() const
{
    return destinationByLabel_;
}

const AddressBook& AddressBookManager::getAddressBook() const
{
    return mapAddressBook;
}
bool AddressBookManager::setAddressLabel(const CTxDestination& address, const std::string newLabel)
{
    bool updatesExistingLabel = mapAddressBook.find(address) != mapAddressBook.end();
    mapAddressBook[address].name = newLabel;
    destinationByLabel_[newLabel] = address;
    return updatesExistingLabel;
}