#ifndef FILESYSTEM_TEST_PROXY_H
#define FILESYSTEM_TEST_PROXY_H
#include <sstream>
#include <unordered_map>
#include <vector>
#include <ctime>
#include <string>
#include <cstring>

#include <iostream>
#include <unordered_set>
#include <algorithm>

class File
{
public:
    File()
    {
    }

    File(
        std::string in_name, 
        std::time_t in_modifiedTime,
        bool in_isDirectory,
        File* in_parent = NULL
        ): name(in_name)
        , modifiedTime(in_modifiedTime)
        , isDirectory(in_isDirectory)
        , parent(in_parent)
    {
    }

    virtual ~File()
    {
    }

    std::string name = "";
    std::time_t modifiedTime = std::time_t(0);
    bool isDirectory = false;

    std::string getFullPath() const;
    
    void setParent(File* newParent);
    File* getParent();
private:
    File* parent = NULL;
};


bool operator==(const File& a, const File& b);

struct FileHasher
{
    size_t operator()(const File& properties) const
    {
        std::stringstream ss;
        ss << properties.getFullPath();
        ss << properties.isDirectory;

        return std::hash<std::string>()(ss.str());
    }
};

class Directory: public File
{
public:
    Directory()
    {
        isDirectory = true;
    }
    Directory(
        std::string in_name, 
        std::time_t in_modifiedTime,
        File* in_parent
        ): File(in_name,in_modifiedTime,true,in_parent)
    {
    }

    bool addFile(File* newFile);

    bool containsFilename(std::string filename);

    bool removeFromFolder(std::string name);

    std::vector<File*> deleteContentsAndGetFileReferences();
    std::vector<File*> getContainedFiles();

private:
    std::vector<File*> containedFiles_;
    std::unordered_set<std::string> containedFileNames_;
};

class FileSystemTree
{
private:
    typedef std::unordered_map<
        File, File*, 
        FileHasher> FileSystemHashMap;
    FileSystemHashMap fileSystemHashMap_;
    Directory root_;
public:
    FileSystemTree(
        ): root_()
    {
    }

    ~FileSystemTree()
    {
        for(auto fileRegistry: fileSystemHashMap_)
        {
            if(fileRegistry.second != NULL)
            {
                delete fileRegistry.second;
                fileRegistry.second = NULL;
            }
        }
        fileSystemHashMap_.clear();
    }

    std::pair<Directory*,bool> addDirectory(Directory& directory);

    std::pair<Directory*,bool> addDirectory(std::string name,std::time_t timestamp, Directory* defaultDirectory = NULL);

    std::pair<File*,bool> addFile(std::string name,std::time_t timestamp, Directory* defaultDirectory = NULL);

    bool deleteFile(File file);

    bool deleteFile(File* file);

    bool deleteEmptyDirectory(Directory* directory);

    void deleteDirectory(Directory* directory);

    bool fileOrDirectoryExists(const File& file) const;

    Directory getDirectoryIfExists(const std::string& fullPath) const;

    bool moveFile(const File& file, Directory* from, Directory* to);

    Directory* moveFolder(Directory* from, Directory* into);

};

#endif //FILESYSTEM_TEST_PROXY_H