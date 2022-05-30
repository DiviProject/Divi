#ifndef REST_H
#define REST_H
#include <string>
#include <map>
class AcceptedConnection;
bool HTTPReq_REST(
    bool (*rpcStatusCheck)(std::string* statusMessageRef),
    AcceptedConnection* conn,
    std::string& strURI,
    std::map<std::string, std::string>& mapHeaders,
    bool fRun);
#endif// REST_H