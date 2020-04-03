#ifndef DIRECTORY_NAME_PROVIDER_H
#define DIRECTORY_NAME_PROVIDER_H
#include <string>
#include <boost/filesystem/path.hpp>
#include <map>

extern std::map<std::string, std::string> mapArgs;

boost::filesystem::path GetDefaultDataDir();
const boost::filesystem::path& GetDataDir(bool fNetSpecific = true);
void ClearDatadirCache();

#endif