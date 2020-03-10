#include <mockFileSystem.h>
#include <filesystemtestproxy.h>

bool MockFileSystem::exists(const PathType& path)
{
    return false;
}
unsigned MockFileSystem::remove_all(const PathType& path)
{
    return 0;
}
bool MockFileSystem::remove(const PathType& path)
{
    File file = File(path, 0, false);
    return fileSystem_.deleteFile(file);
}
void MockFileSystem::copy_file(const PathType& from, const PathType& to)
{
    
}
std::time_t MockFileSystem::last_write_time(const PathType& path)
{
    return std::time_t(0);
}
bool MockFileSystem::create_directories(const PathType& path)
{
    return false;
}
void MockFileSystem::rename(const PathType& oldPath, const PathType& newPath)
{
    
}

TimeStampedFilePaths MockFileSystem::get_timestamps(const PathType& path)
{
    Directory directory = fileSystem_.getDirectoryIfExists(path);
    std::vector<File*> directoryContents = directory.getContainedFiles();
    TimeStampedFilePaths namedTimestamps;

    unsigned end_iter = directoryContents.size();
    for(File* content: directoryContents)
    {
        //i translate to ith element of directory
        std::time_t timeStamp = content -> modifiedTime;
        std::string name = content -> name;
        
        namedTimestamps.push_back(std::make_pair(timeStamp, name));
    }
    
    //return all timestamps
    return namedTimestamps;
}
