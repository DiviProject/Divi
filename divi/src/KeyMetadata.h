#ifndef KEY_METADATA_H
#define KEY_METADATA_H
#include <stdint.h>
#include <string>
#include <serialize.h>
class CKeyMetadata
{
public:
    static const int CURRENT_VERSION = 1;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown

    bool unknownKeyID;
    bool isHDPubKey;
    std::string hdkeypath;
    std::string hdchainid;

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(nCreateTime);
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        unknownKeyID = true;
        isHDPubKey = false;
        hdkeypath = "";
        hdchainid = "";
    }
};
#endif// KEY_METADATA_H