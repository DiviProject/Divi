#include <boost/test/unit_test.hpp>
#include <gmock/gmock.h>
#include <FilteredBoostFileSystem.h>
#include <boost/filesystem.hpp>
#include <string>
#include <algorithm>

class DirectoryCreatorHelper 
{
private:
    boost::filesystem::path dummyTestingDirectory;
public:
    DirectoryCreatorHelper()
    {
        dummyTestingDirectory = boost::filesystem::path( boost::filesystem::current_path() ) / "testingDirectory";
        boost::filesystem::create_directories(dummyTestingDirectory);
    }
    ~DirectoryCreatorHelper()
    {
        boost::filesystem::remove_all(dummyTestingDirectory);
    }

    boost::filesystem::path GetTestingDirectory() const
    {
        return dummyTestingDirectory;
    }

    void CreateFile(std::string filename) const
    {
        boost::filesystem::ofstream file(dummyTestingDirectory / filename.c_str());
        file.close();
    }

    void SetLastWriteTime(boost::filesystem::path path, std::time_t newTime)
    {
        boost::filesystem::permissions(path, boost::filesystem::all_all);
        boost::filesystem::last_write_time(path, newTime);
    }

    TimeStampedFolderContents CreateVariousFilesWithSamePrefix(std::string prefix, unsigned int amountOfFiles)
    {
        
        TimeStampedFolderContents generatedContents;
        for(unsigned fileIndex = 0; fileIndex < amountOfFiles; fileIndex++)
        {
            std::string fileName = prefix + "." + std::to_string(fileIndex);
            boost::filesystem::path fullFilePath = dummyTestingDirectory / fileName.c_str();

            CreateFile(fileName);
            
            SetLastWriteTime(fullFilePath, std::time_t(fileIndex));
            
            generatedContents.push_back( std::pair<std::time_t, std::string>{std::time_t(fileIndex), fullFilePath.string()} );
        }
        return generatedContents;
    }
};

BOOST_AUTO_TEST_SUITE(BoostFileSystemTests)

BOOST_AUTO_TEST_CASE(checkHelperWorks)
{
    DirectoryCreatorHelper helper;

    BOOST_CHECK(boost::filesystem::exists(helper.GetTestingDirectory()));
}

BOOST_AUTO_TEST_CASE(ifDirectoryDoesNotExistFolderContentsAreEmpty)
{
    DirectoryCreatorHelper helper;
    FilteredBoostFileSystem filteredBoostFileSystem("wallet.dat");
    boost::filesystem::path fakeDirectory(helper.GetTestingDirectory() / "fakeDirectory");
    TimeStampedFolderContents empty_set;
    EXPECT_TRUE(filteredBoostFileSystem.get_timestamped_folder_contents( fakeDirectory.string() ).empty());
}

BOOST_AUTO_TEST_CASE(ifDirectoryDoesExistAndHasContentsFolderContentsAreNotEmpty)
{
    DirectoryCreatorHelper helper;
    FilteredBoostFileSystem filteredBoostFileSystem("wallet.dat");
    helper.CreateFile("wallet.dat.whatever");

    EXPECT_FALSE(filteredBoostFileSystem.get_timestamped_folder_contents( helper.GetTestingDirectory().string() ).empty());
}

BOOST_AUTO_TEST_CASE(ifDirectoryDoesExistAndHasContentsFolderContentsAreNotEmptyAndHaveCorrectTimestamps)
{
    DirectoryCreatorHelper helper;
    FilteredBoostFileSystem filteredBoostFileSystem("wallet.dat");

    TimeStampedFolderContents expectedFolderContents = helper.CreateVariousFilesWithSamePrefix("wallet.dat", 10);
    typedef std::pair<std::time_t, std::string> TimeStampedPath;
    TimeStampedFolderContents timeStampedFolderContents = filteredBoostFileSystem.get_timestamped_folder_contents( helper.GetTestingDirectory().string());
    std::sort(timeStampedFolderContents.begin(), timeStampedFolderContents.end(), [](const TimeStampedPath& a, const TimeStampedPath& b){
        return a.first < b.first;
    });

    EXPECT_EQ(timeStampedFolderContents.size(), expectedFolderContents.size());

    EXPECT_TRUE(std::equal(timeStampedFolderContents.begin(), timeStampedFolderContents.end(), expectedFolderContents.begin()));
}


BOOST_AUTO_TEST_SUITE_END()