// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "flat-database.h"

std::unique_ptr<FlatDataFile::Reader> FlatDataFile::Read() const
{
    std::unique_ptr<Reader> reader(new Reader(Path()));
    if (reader->GetFile().IsNull())
    {
        error("%s: Failed to open file %s", __func__, Path().string());
        return nullptr;
    }
    return reader;
}

FlatDataFile::ReadResult FlatDataFile::Reader::NextChunk(const size_t dataSize)
{
    std::vector<unsigned char> vchData(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        file.read((char *)&vchData[0], vchData.size());
        file >> hashIn;
    } catch (const std::exception& e) {
        error("%s: Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }

    chunk.reset(new CDataStream(vchData, SER_DISK, CLIENT_VERSION));

    // verify stored checksum matches input data
    const uint256 hashTmp = Hash(chunk->begin(), chunk->end());
    if (hashIn != hashTmp)
    {
        error("%s: Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    try {
        // de-serialize file header (file specific magic message) and ..
        *chunk >> magic;

        // de-serialize file header (network specific magic number) and ..
        unsigned char pchMsgTmp[4];
        *chunk >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s: Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
    } catch (const std::exception& e) {
        error("%s: Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    return Ok;
}

std::unique_ptr<AppendOnlyFile::Reader> AppendOnlyFile::Read() const
{
    std::unique_ptr<FlatDataFile::Reader> flatFileReader = FlatDataFile::Read();
    if (flatFileReader == nullptr)
        return nullptr;
    return std::unique_ptr<Reader>(new Reader(std::move(flatFileReader)));
}

bool AppendOnlyFile::Reader::Next()
{
    uint64_t dataSize;
    try {
        reader->GetFile() >> dataSize;
    } catch (const std::exception& e) {
        if (std::feof(reader->GetFile().Get()))
            return false;
        return error("%s: Error reading data file - %s", __func__, e.what());
    }

    return reader->NextChunk(dataSize) == Ok;
}
