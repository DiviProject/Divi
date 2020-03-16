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

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

std::vector<std::string> split(const std::string& s, const std::string& delimiter, const bool& removeEmptyEntries = false)
{
    std::vector<std::string> tokens;

    for (size_t start = 0, end; start < s.length(); start = end + delimiter.length())
    {
         size_t position = s.find(delimiter, start);
         end = position != std::string::npos ? position : s.length();

         std::string token = s.substr(start, end - start);
         if (!removeEmptyEntries || !token.empty())
         {
             tokens.push_back(token);
         }
    }
    
    if (!removeEmptyEntries &&
        (s.empty() || endsWith(s, delimiter)))
    {
        tokens.push_back("");
    }

    return tokens;
}
std::string getFolderPath(const PathType& to)
{
    std::vector<std::string> splitString = split(to, "/");
    return splitString[ splitString.size() - 2 ];
}

void MockFileSystem::copy_file(const PathType& from, const PathType& to)
{
    if(exists(from))
    {
        addExistsMapping(to, true);
        std::string folderPath = getFolderPath(to);
        timestampsMapping[folderPath].push_back(std::make_pair( timestampTracker++, to ));
    }
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
