#ifndef ADDR_DB_H
#define ADDR_DB_H
#include <boost/filesystem/path.hpp>
class CAddrMan;
/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    boost::filesystem::path pathAddr;

public:
    CAddrDB();
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};
#endif// ADDR_DB_H