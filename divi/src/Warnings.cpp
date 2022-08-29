#include <Warnings.h>

#include <clientversion.h>
#include <alert.h>

static std::string miscellaneousWarnings;
static bool fLargeWorkForkFound = false;
static bool fLargeWorkInvalidChainFound = false;

bool Warnings::haveFoundLargeWorkFork()
{
    return fLargeWorkForkFound;
}

void Warnings::setLargeWorkForkFound(bool updatedValue)
{
    fLargeWorkForkFound = updatedValue;
}
void Warnings::setLargeWorkInvalidChainFound(bool updatedValue)
{
    fLargeWorkInvalidChainFound = updatedValue;
}

std::pair<std::string,std::string> Warnings::GetWarnings(bool safeMode, int& alertPriority)
{
    alertPriority = 0;
    std::string strStatusBar;
    std::string strRPC;

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = std::string("This is a pre-release test build - use at your own risk - do not use for staking or merchant applications!");

    if (safeMode)
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (miscellaneousWarnings != "") {
        alertPriority = 1000;
        strStatusBar = miscellaneousWarnings;
    }

    if (fLargeWorkForkFound) {
        alertPriority = 2000;
        strStatusBar = strRPC = std::string("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    } else if (fLargeWorkInvalidChainFound) {
        alertPriority = 2000;
        strStatusBar = strRPC = std::string("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    return std::make_pair(strStatusBar,strRPC);
}
void Warnings::setMiscWarning(std::string currentMiscellaneousWarning)
{
    miscellaneousWarnings = currentMiscellaneousWarning;
}
