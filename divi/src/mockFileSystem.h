#ifndef MOCKFILESYSTEM_H
#define MOCKFILESYSTEM_H
#include <i_filesystem.h>
#include <gmock/gmock.h>

class MockFileSystem : public I_FileSystem
{
public:
    MockFileSystem();

    virtual ~MockFileSystem();

    MOCK_METHOD2(rename, void(const PathType&,const PathType&));
    MOCK_METHOD1(exists, bool(const PathType&));
    MOCK_METHOD1(remove_all, unsigned(const PathType& path));
    MOCK_METHOD1(remove, bool(const PathType& path));
    MOCK_METHOD2(copy_file, void(const PathType&, const PathType&));
    MOCK_METHOD1(last_write_time, std::time_t(const PathType&));
    MOCK_METHOD1(create_directories, bool(const PathType&));
    MOCK_METHOD1(get_timestamped_folder_contents, TimeStampedFolderContents(const PathType&));
};

#endif //MOCKFILESYSTEM_H