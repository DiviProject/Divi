#ifndef ADDRESS_BOOK_MANAGER_H
#define ADDRESS_BOOK_MANAGER_H
#include <string>
#include <script/standard.h>
class AddressLabel
{
public:
    std::string name;
    AddressLabel()
    {
    }
};
class AddressBook final: public std::map<CTxDestination, AddressLabel>
{
};
class AddressBookManager
{
public:
    typedef std::map<std::string,CTxDestination> LastDestinationByLabel;

private:
    AddressBook mapAddressBook;
    LastDestinationByLabel destinationByLabel_;
public:
    AddressBookManager();
    const AddressBook& GetAddressBook() const;
    const LastDestinationByLabel& GetLastDestinationByLabel() const;
    bool SetAddressLabel(
        const CTxDestination& address,
        const std::string strName);
};
#endif// ADDRESS_BOOK_MANAGER_H