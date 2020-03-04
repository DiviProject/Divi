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

    std::string getFullPath() const
    {
        if(parent!=NULL)
        {
            return parent->getFullPath() + "/" + name;
        }
        else
        {
            return "/"+name;
        }
    }
    void setParent(File* newParent)
    {
        parent = newParent;
    }
    File* getParent()
    {
        return parent;
    }
private:
    File* parent = NULL;
};


bool operator==(const File& a, const File& b)
{
    std::string fullPathOfFirst =a.getFullPath();
    std::string fullPathOfSecond =b.getFullPath();
    return strcmp(fullPathOfFirst.c_str(),fullPathOfSecond.c_str())==0 &&
        a.modifiedTime == b.modifiedTime &&
        a.isDirectory == b.isDirectory;
}

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

    bool addFile(File* newFile)
    {
        if(!newFile->isDirectory &&
            containedFileNames_.find(newFile->name)== containedFileNames_.end())
        {
            newFile->setParent(this);
            containedFiles_.push_back(newFile);
            containedFileNames_.insert(newFile->name);
            return true;
        }
        return false;
    }

    bool containsFilename(std::string filename)
    {
        if(containedFileNames_.find(filename)== containedFileNames_.end())
        {
            return false;
        }
        return true;
    }

    bool removeFromFolder(std::string name)
    {
        bool removed = false;
        auto it = std::find_if(containedFiles_.begin(),containedFiles_.end(),
            [name](const File* file){ return strcmp(file->name.c_str(),name.c_str())==0;} );
        for(; it != containedFiles_.end();)
        {
            File* file = *it;
            if( strcmp(file->name.c_str(),name.c_str())==0 )
            {
                containedFileNames_.erase(file->name);
                it = containedFiles_.erase(it);
                removed = true;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    std::vector<File*> deleteContentsAndGetFileReferences()
    {
        std::vector<File*> fileReferences = containedFiles_;
        containedFiles_.clear();
        containedFileNames_.clear();
        return fileReferences;
    }
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

    std::pair<Directory*,bool> addDirectory(Directory& directory)
    {
        auto it = fileSystemHashMap_.find(directory);
        if(it == fileSystemHashMap_.end())
        {
            Directory* directoryPtr = new Directory(directory);
            fileSystemHashMap_[directory] = directoryPtr;
            return std::make_pair(directoryPtr,true);
        }
        Directory* nullDirectory = NULL;
        return std::make_pair(nullDirectory,false);
    }
    std::pair<Directory*,bool> addDirectory(std::string name,std::time_t timestamp, Directory* defaultDirectory = NULL)
    {
        if(!defaultDirectory) defaultDirectory  = &root_;
        Directory directory(name, timestamp, defaultDirectory);
        return addDirectory(directory);
    }

    std::pair<File*,bool> addFile(std::string name,std::time_t timestamp, Directory* defaultDirectory = NULL)
    {
        if(!defaultDirectory) defaultDirectory  = &root_;
        File file(name, timestamp, false, static_cast<File*>(defaultDirectory) );
        auto it = fileSystemHashMap_.find(file);
        if(it == fileSystemHashMap_.end())
        {
            if(!defaultDirectory->containsFilename(file.name))
            {
                File* newFile = new File(file);
                fileSystemHashMap_[*newFile] = newFile;
                defaultDirectory->addFile(newFile);
                return std::make_pair(newFile,true);
            }
        }
        File* nullFile = NULL;
        return std::make_pair(nullFile,false);
    }

    bool deleteFile(File file)
    {
        return deleteFile(&file);
    }
    bool deleteFile(File* file)
    {
        auto it = fileSystemHashMap_.find(*file);
        if(it != fileSystemHashMap_.end())
        {
            Directory* parentDirectory = reinterpret_cast<Directory*>(it->second->getParent());
            parentDirectory->removeFromFolder(it->second->name);
            delete it->second;
            it->second = NULL;
            fileSystemHashMap_.erase(it);
            return true;
        }
        return false;
    }

    bool deleteEmptyDirectory(Directory* directory)
    {
        auto it = fileSystemHashMap_.find(*directory);
        if(it != fileSystemHashMap_.end())
        {
            delete it->second;
            it->second = NULL;
            fileSystemHashMap_.erase(it);
            return true;
        }
        return false;
    }

    void deleteDirectory(Directory* directory)
    {
        auto it = fileSystemHashMap_.find(*directory);
        if(it != fileSystemHashMap_.end())
        {
            for(File* containedFile: directory->deleteContentsAndGetFileReferences())
            {
                deleteFile(containedFile);
            }
            deleteEmptyDirectory(directory);
        }
    }

    bool fileOrDirectoryExists(const File& file) const
    {
        auto it = fileSystemHashMap_.find(file);
        if(it != fileSystemHashMap_.end())
        {
            return true;
        }
        return false;
    }

    bool moveFile(const File& file, Directory* from, Directory* to)
    {
        auto it = fileSystemHashMap_.find(file);
        bool directoriesExist = fileSystemHashMap_.find(*from) != fileSystemHashMap_.end() &&
            fileSystemHashMap_.find(*to) != fileSystemHashMap_.end();
        if(it != fileSystemHashMap_.end() && directoriesExist)
        {
            if(!from->containsFilename(file.name)) return false;
            
            if(addFile(file.name,file.modifiedTime,to).second)
            {
                from->removeFromFolder(file.name);
                deleteFile(file);
                return true;
            }
            else
            {
                return false;
            }
        }
        return false;
    }

    Directory* moveFolder(Directory* from, Directory* into)
    {
        auto itSource = fileSystemHashMap_.find(*from);
        auto itDestination = fileSystemHashMap_.find(*into);
        bool directoriesExist = itSource != fileSystemHashMap_.end() &&
            itDestination != fileSystemHashMap_.end();
        if(directoriesExist)
        {
            Directory subdirectory = *from;
            subdirectory.setParent(into);
            std::pair<Directory*, bool> directoryAddition = addDirectory(subdirectory);
            if(directoryAddition.second)
            {
                deleteDirectory(from);
                return directoryAddition.first;
            }
            else
            {
                return NULL;
            }
        }
        return NULL;
    }

};

#endif