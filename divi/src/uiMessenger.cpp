#include <uiMessenger.h>
#include <ui_interface.h>

std::string UIMessenger::translate(const char* psz)
{
    boost::optional<std::string> rv = uiInterface_.Translate(psz);
    return rv ? (*rv) : psz;
}
std::string UIMessenger::translate(std::string psz)
{
    return translate(psz.c_str());
}
std::string UIMessenger::translate(const std::string& translatable, const std::string& untranslatable)
{
    return translate(translatable) + untranslatable;
}

bool UIMessenger::InitError(const std::string& str, std::string untranslateableString)
{
    uiInterface_.ThreadSafeMessageBox(translate(str,untranslateableString), "", CClientUIInterface::MSG_ERROR);
    return false;
}
bool UIMessenger::InitWarning(const std::string& str, std::string untranslateableString)
{
    uiInterface_.ThreadSafeMessageBox(translate(str,untranslateableString), "", CClientUIInterface::MSG_WARNING);
    return true;
}
bool UIMessenger::InitMessage(const std::string& str, std::string untranslateableString)
{
    uiInterface_.InitMessage(translate(str,untranslateableString));
    return true;
}
