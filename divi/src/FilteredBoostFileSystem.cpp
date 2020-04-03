#include <FilteredBoostFileSystem.h>
#include <ctime>
#include <map>
#include <boost/filesystem.hpp>

void FilteredBoostFileSystem::rename(const PathType& oldPath, const PathType& newPath) 
{
    boost::filesystem::rename(oldPath, newPath);
}
bool FilteredBoostFileSystem::exists(const PathType& path)
{
    return boost::filesystem::exists(path);
}
unsigned FilteredBoostFileSystem::remove_all(const PathType& path)
{
    return boost::filesystem::remove_all(path);
}
bool FilteredBoostFileSystem::remove(const PathType& path)
{
    return boost::filesystem::remove(path);
}
void FilteredBoostFileSystem::copy_file(const PathType& from, const PathType& to)
{
    boost::filesystem::copy_file(from,to);
}
std::time_t FilteredBoostFileSystem::last_write_time(const PathType& path)
{
    return boost::filesystem::last_write_time(path);
}
bool FilteredBoostFileSystem::create_directories(const PathType& path)
{
    return boost::filesystem::create_directories(path);
}
TimeStampedFolderContents FilteredBoostFileSystem::get_timestamped_folder_contents(const PathType& directory)
{
    // Keep only the last 10 backups, including the new one of course
    TimeStampedFolderContents folder_set;
    if(!exists(directory))
    {
        return folder_set;
    }
    boost::filesystem::directory_iterator end_iter;
    boost::filesystem::path directoryPath = directory;
    directoryPath.make_preferred();
    // Build map of backup files for current(!) wallet sorted by last write time
    for (boost::filesystem::directory_iterator dir_iter(directoryPath); dir_iter != end_iter; ++dir_iter) {
        // Only check regular files
        if (boost::filesystem::is_regular_file(dir_iter->status())) {
            // Only add the backups for the current wallet, e.g. wallet.dat.*
            if (dir_iter->path().stem().string() == filenameFilter_) {
                folder_set.push_back(std::pair<std::time_t, PathType>{boost::filesystem::last_write_time(dir_iter->path()), dir_iter->path().string()});
            }
        }
    }
    return folder_set;
}