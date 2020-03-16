#ifndef MOCKFILESYSTEM_H
#define MOCKFILESYSTEM_H
#include <i_filesystem.h>
#include <unordered_map>
class FileSystemTree;

class MockFileSystem : public I_FileSystem
{
private:
    std::unordered_map<std::string, bool> existsMapping;
    std::unordered_map<std::string, unsigned> removeAllMapping;
    std::unordered_map<std::string, bool> removeMapping;
    std::unordered_map<std::string, std::time_t> lastWriteTimeMapping;
    std::unordered_map<std::string, bool> createDirectoriesMapping;
    std::unordered_map<std::string, TimeStampedFilePaths> timestampsMapping;
    
    mutable unsigned timestampTracker = 0;

    void clearMappings();
public:
    MockFileSystem()
    {
        clearMappings();
    }

    virtual ~MockFileSystem(){}
    void addExistsMapping (PathType path, bool existence);
    virtual bool exists(const PathType& path);
    
    void addRemoveAllMapping (PathType path, unsigned numberRemoved);
    virtual unsigned remove_all(const PathType& path);
    
    virtual void rename(const PathType& oldPath, const PathType& newPath);
    
    void addRemoveMapping (PathType path, bool removed);
    virtual bool remove(const PathType& path);
    
    virtual void copy_file(const PathType& from, const PathType& to);
    
    void addLastWriteTimeMapping (PathType path, std::time_t time);
    virtual std::time_t last_write_time(const PathType& path);

    void addCreateDirectoriesMapping (PathType path, bool directory);
    virtual bool create_directories(const PathType& path);
    
    void addGetTimestampsMapping (PathType path, TimeStampedFilePaths timestampedPath);
    virtual TimeStampedFilePaths get_timestamps(const PathType& path);
};

#endif //MOCKFILESYSTEM_H