// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

class COutPoint;
class Settings;

class CMasternodeConfig
{
public:
    class CMasternodeEntry
    {
    private:
        std::string alias;
        std::string ip;
        std::string privKey;
        std::string txHash;
        std::string outputIndex;

    public:
        CMasternodeEntry(const std::string& alias, const std::string& ip,
                         const std::string& privKey,
                         const std::string& txHash, const std::string& outputIndex)
        {
            this->alias = alias;
            this->ip = ip;
            this->privKey = privKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
        }

        const std::string& getAlias() const
        {
            return alias;
        }

        void setAlias(const std::string& alias)
        {
            this->alias = alias;
        }

        const std::string& getOutputIndex() const
        {
            return outputIndex;
        }

        void setOutputIndex(const std::string& outputIndex)
        {
            this->outputIndex = outputIndex;
        }

        const std::string& getPrivKey() const
        {
            return privKey;
        }

        void setPrivKey(const std::string& privKey)
        {
            this->privKey = privKey;
        }

        const std::string& getTxHash() const
        {
            return txHash;
        }

        void setTxHash(const std::string& txHash)
        {
            this->txHash = txHash;
        }

        const std::string& getIp() const
        {
            return ip;
        }

        void setIp(const std::string& ip)
        {
            this->ip = ip;
        }

        /** Tries to parse the entry's input reference into an outpoint.
         *  Returns true on success.  */
        bool parseInputReference(COutPoint& outp) const;
    };

    CMasternodeConfig();

    void clear();
    bool read(const Settings& settings, std::string& strErr);
    void add(const std::string& alias, const std::string& ip, const std::string& privKey,
             const std::string& txHash, const std::string& outputIndex);
    const std::vector<CMasternodeEntry>& getEntries() const;
    int getCount() const;

private:
    std::vector<CMasternodeEntry> entries;
};


#endif /* SRC_MASTERNODECONFIG_H_ */
