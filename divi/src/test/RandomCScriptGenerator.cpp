#include <RandomCScriptGenerator.h>

RandomCScriptGenerator::RandomCScriptGenerator(
    ): distribution_(0, 255)
{
}

CScript RandomCScriptGenerator::operator()(unsigned scriptLength) const
{
    std::vector<unsigned char> scriptBytes;
    scriptBytes.resize(scriptLength);
    for(unsigned scriptByteIndex = 0; scriptByteIndex < scriptLength; ++scriptByteIndex)
    {
        scriptBytes[scriptByteIndex] = static_cast<unsigned char>(distribution_(randomnessEngine_));
    }
    return CScript(scriptBytes.begin(),scriptBytes.end());
}