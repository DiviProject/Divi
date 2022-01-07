#include <BlockFileHelpers.h>

#include <sync.h>
#include <util.h>
#include <BlockDiskPosition.h>
#include <BlockFileOpener.h>

void BlockFileHelpers::FlushBlockFile(int nLastBlockFile, const std::vector<CBlockFileInfo>& vinfoBlockFile, bool fFinalize)
{
    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE* fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}
