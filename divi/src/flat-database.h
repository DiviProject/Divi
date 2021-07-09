// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FLAT_DATABASE_H
#define FLAT_DATABASE_H

#include "chainparams.h"
#include "clientversion.h"
#include "hash.h"
#include "streams.h"
#include "utiltime.h"
#include "Logging.h"
#include "DataDirectory.h"

#include <boost/filesystem.hpp>

/** Base class for file-based data storage.  It supports generic
 *  reading and writing from/to files in checksumed chunks of data.  */
class FlatDataFile
{

private:

    const std::string strFilename;
    const boost::filesystem::path pathDB;

public:

    class Reader;

    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    explicit FlatDataFile(const std::string& strFilenameIn)
      : strFilename(strFilenameIn), pathDB(GetDataDir() / strFilenameIn)
    {}

    const std::string& Filename() const
    {
        return strFilename;
    }

    const boost::filesystem::path& Path() const
    {
        return pathDB;
    }

    /** Writes a single chunk of data to the file, using
     *  the given magic bytes and optionally appending rather
     *  than truncating the file.  */
    template <typename T>
        bool Write(const std::string& magic, const T& objToSave, bool append) const
    {
        // LOCK(objToSave.cs);

        const int64_t nStart = GetTimeMillis();

        // serialize, checksum data up to that point, then append checksum
        CDataStream ssObj(SER_DISK, CLIENT_VERSION);
        ssObj << magic; // specific magic message for this type of object
        ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
        ssObj << objToSave;
        const uint256 hash = Hash(ssObj.begin(), ssObj.end());
        ssObj << hash;

        // open output file, and associate with CAutoFile
        FILE *file = fopen(Path().string().c_str(), append ? "ab" : "wb");
        CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
        if (fileout.IsNull())
            return error("%s: Failed to open file %s", __func__, Path().string());

        // Write and commit header, data
        try {
            fileout << ssObj;
        } catch (const std::exception& e) {
            return error("%s: Serialize or I/O error - %s", __func__, e.what());
        }
        fileout.fclose();

        LogPrintf("Written info to %s  %dms\n", Filename(), GetTimeMillis() - nStart);
        LogPrintf("     %s\n", objToSave.ToString());

        return true;
    }

    /** Returns a reader instance for the underlying file.  */
    std::unique_ptr<Reader> Read() const;

};

/** Helper class that corresponds to an opened data file and allows to
 *  read it chunk-by-chunk.  This is a bit like an iterator.  */
class FlatDataFile::Reader
{

private:

    /** The underlying opened file.  */
    CAutoFile file;

    /** The magic string of the current chunk.  */
    std::string magic;

    /** If there is a current chunk of unparsed data, this holds
     *  the stream to read.  */
    std::unique_ptr<CDataStream> chunk;

    explicit Reader(const boost::filesystem::path& pathDB)
      : file(fopen(pathDB.string().c_str(), "rb"), SER_DISK, CLIENT_VERSION)
    {}

    friend class FlatDataFile;

public:

    CAutoFile& GetFile()
    {
        return file;
    }

    /** Tries to read the next chunk of raw data from the file.  This parses
     *  the magic, verifies the network version and checks the hash, but does
     *  not yet attempt to parse the data into a particular instance.
     *
     *  The caller must pass in the expected size of the next chunk, which they
     *  must obtain somehow themselves.  */
    ReadResult NextChunk(size_t dataSize);

    /** Returns the magic of the current chunk.  */
    const std::string& GetMagic() const
    {
        assert(chunk != nullptr);
        return magic;
    }

    /** Parses the current chunk into an object of type T.  */
    template <typename T>
        ReadResult ParseChunk(T& objToLoad)
    {
        assert(chunk != nullptr);
        try {
            // de-serialize data into T object
            *chunk >> objToLoad;
            chunk.reset();
        } catch (const std::exception& e) {
            objToLoad.Clear();
            error("%s: Deserialize or I/O error - %s", __func__, e.what());
            return IncorrectFormat;
        }
        return Ok;
    }

};

/** Flat data file that stores a single chunk and is thus able to dump
 *  and load one instance of some class T.  */
template<typename T>
class CFlatDB : private FlatDataFile
{

private:

    const std::string strMagicMessage;

    ReadResult Read(T& objToLoad, const bool fDryRun = false) const
    {
        const int64_t nStart = GetTimeMillis();

        auto reader = FlatDataFile::Read();
        if (reader == nullptr)
            return FileError;

        // use file size to size memory buffer
        const int fileSize = boost::filesystem::file_size(Path());
        int dataSize = fileSize - sizeof(uint256);
        // Don't try to resize to a negative number if file is small
        if (dataSize < 0)
            dataSize = 0;

        ReadResult res = reader->NextChunk(dataSize);
        if (res != Ok)
            return res;

        if (reader->GetMagic() != strMagicMessage)
        {
            error("%s: Invalid magic message", __func__);
            return IncorrectMagicMessage;
        }

        res = reader->ParseChunk(objToLoad);
        if (res != Ok)
            return res;

        LogPrintf("Loaded info from %s  %dms\n", Filename(), GetTimeMillis() - nStart);
        LogPrintf("     %s\n", objToLoad.ToString());
        if(!fDryRun) {
            LogPrintf("%s: Cleaning....\n", __func__);
            objToLoad.CheckAndRemove();
            LogPrintf("     %s\n", objToLoad.ToString());
        }

        return Ok;
    }

public:

    explicit CFlatDB(const std::string& strFilenameIn, const std::string& strMagicMessageIn)
      : FlatDataFile(strFilenameIn),
        strMagicMessage(strMagicMessageIn)
    {}

    bool Load(T& objToLoad) const
    {
        LogPrintf("Reading info from %s...\n", Filename());
        ReadResult readResult = Read(objToLoad);
        if (readResult == FileError)
            LogPrintf("Missing file %s, will try to recreate\n", Filename());
        else if (readResult != Ok)
        {
            LogPrintf("Error reading %s: ", Filename());
            if(readResult == IncorrectFormat)
            {
                LogPrintf("%s: Magic is ok but data has invalid format, will try to recreate\n", __func__);
            }
            else {
                LogPrintf("%s: File format is unknown or invalid, please fix it manually\n", __func__);
                // program should exit with an error
                return false;
            }
        }
        return true;
    }

    bool Dump(T& objToSave) const
    {
        const int64_t nStart = GetTimeMillis();

        LogPrintf("Verifying %s format...\n", Filename());
        T tmpObjToLoad;
        ReadResult readResult = Read(tmpObjToLoad, true);

        // there was an error and it was not an error on file opening => do not proceed
        if (readResult == FileError)
            LogPrintf("Missing file %s, will try to recreate\n", Filename());
        else if (readResult != Ok)
        {
            LogPrintf("Error reading %s: ", Filename());
            if(readResult == IncorrectFormat)
                LogPrintf("%s: Magic is ok but data has invalid format, will try to recreate\n", __func__);
            else
            {
                LogPrintf("%s: File format is unknown or invalid, please fix it manually\n", __func__);
                return false;
            }
        }

        LogPrintf("Writing info to %s...\n", Filename());
        Write(strMagicMessage, objToSave, false);
        LogPrintf("%s dump finished  %dms\n", Filename(), GetTimeMillis() - nStart);

        return true;
    }

};


#endif
