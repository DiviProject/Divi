#include <AddressBookManager.h>


AddressBookManager::AddressBookManager(): mapAddressBook(), destinationByLabel_()
{
}

const AddressBookManager::LastDestinationByLabel& AddressBookManager::GetLastDestinationByLabel() const
{
    return destinationByLabel_;
}

const AddressBook& AddressBookManager::GetAddressBook() const
{
    return mapAddressBook;
}
bool AddressBookManager::SetAddressLabel(const CTxDestination& address, const std::string newLabel)
{
    bool updatesExistingLabel = mapAddressBook.find(address) != mapAddressBook.end();
    mapAddressBook[address].name = newLabel;
    destinationByLabel_[newLabel] = address;
    return updatesExistingLabel;
}