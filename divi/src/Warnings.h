#ifndef WARNINGS_H
#define WARNINGS_H
#include <string>
namespace Warnings
{
    bool haveFoundLargeWorkFork();
    void setLargeWorkForkFound(bool updatedValue);
    void setLargeWorkInvalidChainFound(bool updatedValue);
    std::pair<std::string,std::string> GetWarnings(bool safeMode, int& alertPriority);
    void setMiscWarning(std::string strFor);
};
#endif// WARNINGS_H