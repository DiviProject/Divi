#ifndef MOCKFILESYSTEM_H
#define MOCKFILESYSTEM_H
#include <i_filesystem.h>


class MockFileSystem : public I_FileSystem
{
public:
    virtual void rename(const PathType& oldPath, const PathType& newPath);
    virtual bool exists(const PathType& path);
    virtual unsigned remove_all(const PathType& path);
    virtual void copy_file(const PathType& from, const PathType& to);
    virtual std::time_t last_write_time(const PathType& path);
    virtual bool create_directories(const PathType& path);

}

#endif //MOCKFILESYSTEM_H