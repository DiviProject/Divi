#ifndef BLOCK_FILE_OPENER_H
#define BLOCK_FILE_OPENER_H

#include <cstdio>
class CDiskBlockPos;

bool BlockFileExists(const CDiskBlockPos& pos, const char* prefix);
FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly = false);
FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly = false);

#endif // BLOCK_FILE_OPENER_H