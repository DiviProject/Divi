#ifndef FILTERED_BOOST_FILE_SYSTEM_H
#define FILTERED_BOOST_FILE_SYSTEM_H

#include <i_filesystem.h>
#include <string>

class FilteredBoostFileSystem : public I_FileSystem 
{
private:
    std::string filenameFilter_;

public:
    FilteredBoostFileSystem (
        std::string filenameFilter
        ): filenameFilter_(filenameFilter) 
    {

    }
    virtual ~FilteredBoostFileSystem () {};
    virtual void rename(const PathType& oldPath, const PathType& newPath);
    virtual bool exists(const PathType& path);
    virtual unsigned remove_all(const PathType& path);
    virtual bool remove(const PathType& path);
    virtual void copy_file(const PathType& from, const PathType& to);
    virtual std::time_t last_write_time(const PathType& path);
    virtual bool create_directories(const PathType& path);
    virtual TimeStampedFolderContents get_timestamped_folder_contents(const PathType& directory);

};
#endif //FILTERED_BOOST_FILE_SYSTEM_H