#include <mockFileSystem.h>


void MockFileSystem::clearMappings()
{
    existsMapping.clear();
    removeAllMapping.clear();
    removeMapping.clear();
    lastWriteTimeMapping.clear();
    createDirectoriesMapping.clear();
    timestampsMapping.clear();
}

void MockFileSystem::addExistsMapping(PathType path, bool existence)
{
    existsMapping[path] = existence;
}

bool MockFileSystem::exists(const PathType& path)
{
    auto it = existsMapping.find(path);
    
    return (it!=existsMapping.end())? it->second: false;
}

void MockFileSystem::addRemoveAllMapping(PathType path, unsigned numberRemoved)
{
    existsMapping[path] = numberRemoved;
}
unsigned MockFileSystem::remove_all(const PathType& path)
{
    auto it = removeAllMapping.find(path);
    
    return (it!=removeAllMapping.end())? it->second: 0;
}


void MockFileSystem::rename(const PathType& oldPath, const PathType& newPath)
{
    
}


void MockFileSystem::addRemoveMapping(PathType path, bool removed)
{
    removeMapping[path] = removed;
}
bool MockFileSystem::remove(const PathType& path)
{
    auto it = removeMapping.find(path);
    
    return (it!=removeMapping.end())? it->second: false;
}

void MockFileSystem::copy_file(const PathType& from, const PathType& to)
{
    
}

void MockFileSystem::addLastWriteTimeMapping (PathType path, std::time_t time)
{
    lastWriteTimeMapping[path] = time;
}
std::time_t MockFileSystem::last_write_time(const PathType& path)
{
    auto it = lastWriteTimeMapping.find(path);
    
    return (it!=lastWriteTimeMapping.end())? it->second: std::time_t(0);
}

void MockFileSystem::addCreateDirectoriesMapping (PathType path, bool directory)
{
    createDirectoriesMapping[path] = directory;
}
bool MockFileSystem::create_directories(const PathType& path)
{
    auto it = createDirectoriesMapping.find(path);
    
    return (it!=createDirectoriesMapping.end())? it->second: false;
}

void MockFileSystem::addGetTimestampsMapping (PathType path, TimeStampedFilePaths timestampedPath)
{
    timestampsMapping[path] = timestampedPath;
}
TimeStampedFilePaths MockFileSystem::get_timestamps(const PathType& path)
{
    auto it = timestampsMapping.find(path);
    
    return (it!=timestampsMapping.end())? it->second: TimeStampedFilePaths();
}
