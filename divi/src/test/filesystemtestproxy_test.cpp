#include "filesystemtestproxy.h"
#include <boost/test/unit_test.hpp>

#define EQUAL_STR(x,y) strcmp((x).c_str(),(y).c_str())==0

BOOST_AUTO_TEST_SUITE(FileSystemProxy)

BOOST_AUTO_TEST_CASE(willAddFile)
{
    FileSystemTree filesystem;

    unsigned initialTime = 100;
    std::pair<File*,bool> fileDataAndStatus = filesystem.addFile(std::string("aFile.txt") , std::time_t(initialTime));
    BOOST_CHECK(fileDataAndStatus.second);
    if(fileDataAndStatus.second)
    {
        BOOST_CHECK(filesystem.fileOrDirectoryExists(*fileDataAndStatus.first) );
    }
}

BOOST_AUTO_TEST_CASE(willRemoveAddedFile)
{
    FileSystemTree filesystem;

    unsigned initialTime = 100;
    auto firstFile = filesystem.addFile( std::string("AFirstFile.txt") , std::time_t(initialTime)) ;
    auto secondFile = filesystem.addFile(std::string("SomeOtherFile.txt") , std::time_t(initialTime)) ;

    BOOST_CHECK(firstFile.second);
    BOOST_CHECK(secondFile.second);
    File fileToRemove = *firstFile.first;
    File fileToKeep = *secondFile.first;
    filesystem.deleteFile(fileToRemove);
    BOOST_CHECK(!filesystem.fileOrDirectoryExists(fileToRemove));
    BOOST_CHECK(filesystem.fileOrDirectoryExists(fileToKeep));
}


BOOST_AUTO_TEST_CASE(willAddFileInNestedFolder)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    auto directory = filesystem.addDirectory(std::string("SomeOtherFiles") , std::time_t(initialTime)) ;
    auto firstFile = filesystem.addFile( std::string("AFirstFile.txt") , std::time_t(initialTime), directory.first) ;
    File file = *firstFile.first;

    BOOST_CHECK(directory.first->containsFilename(file.name));
    BOOST_CHECK(directory.first->containsFilename(file.name));
}

BOOST_AUTO_TEST_CASE(willDeleteFileInNestedFolder)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    auto directory = filesystem.addDirectory(std::string("SomeOtherFiles") , std::time_t(initialTime)) ;
    auto firstFile = filesystem.addFile( std::string("AFirstFile.txt") , std::time_t(initialTime), directory.first) ;
    File fileToDelete = *firstFile.first;

    filesystem.deleteFile(fileToDelete);

    BOOST_CHECK(!directory.first->containsFilename(fileToDelete.name));
    BOOST_CHECK(!filesystem.fileOrDirectoryExists(fileToDelete));
}


BOOST_AUTO_TEST_CASE(willMoveFileFromOneFolderToAnother)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    auto directory1 = filesystem.addDirectory(std::string("OtherFiles") , std::time_t(initialTime)).first ;
    auto directory2 = filesystem.addDirectory(std::string("SomeOtherFiles") , std::time_t(initialTime)).first ;
    auto firstFile = filesystem.addFile( std::string("AFirstFile.txt") , std::time_t(initialTime), directory1) ;
    File fileToMove = *firstFile.first;

    filesystem.moveFile(fileToMove, directory1,directory2);

    BOOST_CHECK(!directory1->containsFilename(fileToMove.name));
    BOOST_CHECK(directory2->containsFilename(fileToMove.name));

    File expectedFile(std::string("AFirstFile.txt"), fileToMove.modifiedTime, false, directory2);
    BOOST_CHECK(!filesystem.fileOrDirectoryExists(fileToMove));
    BOOST_CHECK(filesystem.fileOrDirectoryExists(expectedFile));
}

BOOST_AUTO_TEST_CASE(willAllowAFileAndADirectoryByTheSameName)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    Directory directory1 = *filesystem.addDirectory(std::string("MyFiles") , std::time_t(initialTime)).first;
    File firstFile = *filesystem.addFile( std::string("Myfiles") , std::time_t(initialTime)).first ;

    BOOST_CHECK(filesystem.fileOrDirectoryExists(directory1));
    BOOST_CHECK(filesystem.fileOrDirectoryExists(firstFile));
}

BOOST_AUTO_TEST_CASE(willDeleteDirectoryAndFilesContained)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    Directory& directory1 = *filesystem.addDirectory(std::string("MyFiles") , std::time_t(initialTime)).first;
    File firstFile = *filesystem.addFile( std::string("Myfiles") , std::time_t(initialTime), &directory1).first ;

    bool directoryStructureExistsPrior = 
        filesystem.fileOrDirectoryExists(directory1) && 
        filesystem.fileOrDirectoryExists(firstFile);

    filesystem.deleteDirectory(&directory1);
    BOOST_CHECK(
        !filesystem.fileOrDirectoryExists(directory1) && 
        !filesystem.fileOrDirectoryExists(firstFile) && 
        directoryStructureExistsPrior);
}

BOOST_AUTO_TEST_CASE(willRebaseDirectoryAndContainedFiles)
{
    FileSystemTree filesystem;
    unsigned initialTime = 100;

    Directory* directory1 = filesystem.addDirectory(std::string("MyFiles") , std::time_t(initialTime)).first;
    Directory* directory2 = filesystem.addDirectory(std::string("OtherFiles") , std::time_t(initialTime)).first;
    File firstFile = *filesystem.addFile( std::string("exampleFile.txt") , std::time_t(initialTime), directory1).first ;

    Directory oldDirectory = *directory1;
    directory1 = filesystem.moveFolder(directory1, directory2);

    BOOST_CHECK(
        !filesystem.fileOrDirectoryExists(oldDirectory));
    BOOST_CHECK(
        directory1->getParent() == directory2);
    BOOST_CHECK( 
        directory1->containsFilename(firstFile.name) );   
}

BOOST_AUTO_TEST_SUITE_END()