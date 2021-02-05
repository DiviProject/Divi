#ifndef UI_MESSENGER_H
#define UI_MESSENGER_H
#include <string>
class CClientUIInterface;

class UIMessenger
{
private:
    CClientUIInterface& uiInterface_;

    inline std::string translate(const char* psz);
    inline std::string translate(std::string psz);
    std::string translate(const std::string& translatable, const std::string& untranslatable);
public:
    UIMessenger(CClientUIInterface& uiInterface_in): uiInterface_(uiInterface_in){}

    bool InitError(const std::string& str, std::string untranslateableString = std::string());
    bool InitWarning(const std::string& str, std::string untranslateableString = std::string());
    bool InitMessage(const std::string& str, std::string untranslateableString = std::string());
};
#endif// UI_MESSENGER_H