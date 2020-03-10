#ifndef MOCKFILESYSTEM_H
#define MOCKFILESYSTEM_H
#include <i_filesystem.h>
#include <filesystemtestproxy.h>


class MockFileSystem : public I_FileSystem
{
private:
    FileSystemTree& fileSystem_;
public:
    MockFileSystem(FileSystemTree& fileSystem) : fileSystem_(fileSystem)

    virtual ~MockFileSystem(){}

    virtual void rename(const PathType& oldPath, const PathType& newPath);
    virtual bool exists(const PathType& path);
    virtual unsigned remove_all(const PathType& path);
    virtual bool remove(const PathType& path);
    virtual void copy_file(const PathType& from, const PathType& to);
    virtual std::time_t last_write_time(const PathType& path);
    virtual bool create_directories(const PathType& path);
    virtual TimeStampedFilePaths get_timestamps(const PathType& path);
}

#endif //MOCKFILESYSTEM_H