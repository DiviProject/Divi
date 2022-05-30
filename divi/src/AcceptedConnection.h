#ifndef ACCEPTED_CONNECTION_H
#define ACCEPTED_CONNECTION_H
#include <tinyformat.h>
#include <string>
class AcceptedConnection
{
public:
    virtual ~AcceptedConnection() {}

    virtual std::iostream& stream() = 0;
    virtual std::string peer_address_to_string() const = 0;
    virtual void close() = 0;
};
#endif// ACCEPTED_CONNECTION_H