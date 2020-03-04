#ifndef I_FILESYSTEM_H
#define I_FILESYSTEM_H
#include <string>
#include <ctime>
typedef std::string PathType;

class I_FileSystem
{
public:
    virtual void rename(const PathType& oldPath, const PathType& newPath) = 0;
    virtual bool exists(const PathType& path) = 0;
    virtual unsigned remove_all(const PathType& path) = 0;
    virtual void copy_file(const PathType& from, const PathType& to) = 0;
    virtual std::time_t last_write_time(const PathType& path) = 0;
    virtual bool create_directories(const PathType& path) = 0;
};

#endif //I_FILESYSTEM_H 