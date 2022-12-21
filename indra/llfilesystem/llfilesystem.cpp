/**
 * @file filesystem.h
 * @brief Simulate local file system operations.
 * @Note The initial implementation does actually use standard C++
 *       file operations but eventually, there will be another
 *       layer that caches and manages file meta data too.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "lldir.h"
#include "llfilesystem.h"
#include "llfasttimer.h"
#include "lldiskcache.h"

#include <boost/filesystem.hpp>

LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
    : mFileID(file_id),
    mFileType(file_type),
    mPosition(0),
    mMode(mode),
    mBytesRead(0)
{
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
#if LL_WINDOWS
    mFilePath = ll_convert_string_to_wide(filename);
#else
    mFilePath = filename;
#endif
}

bool LLFileSystem::open()
{
    switch (mMode)
    {
    case READ: mFile = LLFile::fopen(mFilePath, TEXT("rb")); break;
    case WRITE: mFile = LLFile::fopen(mFilePath, TEXT("wb")); break;
    case READ_WRITE: mFile = LLFile::fopen(mFilePath, TEXT("r+b")); break;
    case APPEND: mFile = LLFile::fopen(mFilePath, TEXT("a+b")); break;
    }
    if (mMode == READ_WRITE && !mFile)
        mFile = LLFile::fopen(mFilePath, TEXT("wb+"));

    if (mFile)
    {
        mPosition = ftell(mFile);
    }

    return mFile;
}

void LLFileSystem::close()
{
    mFile.close();
}

BOOL LLFileSystem::read(U8* buffer, S32 bytes)
{
    if (!mFile)
    {
        LL_WARNS() << "Attempt to read from file " << mFileID << " that is not open" << LL_ENDL;
        return FALSE;
    }

    if (!(mMode & READ))
    {
        LL_WARNS() << "Attempt to read from file " << mFileID << " opened with mode " << std::hex << mMode << std::dec << LL_ENDL;
        return FALSE;
    }

    BOOL success = FALSE;

    if (fseek(mFile, mPosition, SEEK_SET) == 0)
    {
        mBytesRead = fread(buffer, 1, bytes, mFile);

        mPosition = ftell(mFile);

        // It probably would be correct to check for mBytesRead == bytes,
        // but that will break avatar rezzing...
        if (mBytesRead)
        {
            success = TRUE;
        }
    }

    // update the last access time for the file - this is required
    // even though we are reading and not writing because this is the
    // way the cache works - it relies on a valid "last accessed time" for
    // each file so it knows how to remove the oldest, unused files
    updateFileAccessTime();

    return success;
}

S32 LLFileSystem::getLastBytesRead() const
{
    return mBytesRead;
}

BOOL LLFileSystem::eof()
{
    return mPosition >= getSize();
}

BOOL LLFileSystem::write(const U8* buffer, S32 bytes)
{
    if (!mFile)
    {
        LL_WARNS() << "Attempt to write to file " << mFileID << " that is not open" << LL_ENDL;
        return FALSE;
    }

    if (!(mMode & WRITE) || !(mMode & APPEND))
    {
        LL_WARNS() << "Attempt to write to file " << mFileID << " opened with mode " << std::hex << mMode << std::dec << LL_ENDL;
        return FALSE;
    }

    BOOL success = FALSE;
    if (mMode == READ_WRITE)
    {
        if (fseek(mFile, mPosition, SEEK_SET) == 0)
        {
            size_t bytes_written = fwrite((const void*)buffer, 1, bytes, mFile);
            success = (bytes_written == bytes);
        }
    }
    else
    {
        size_t bytes_written = fwrite((const void*)buffer, 1, bytes, mFile);
        success = (bytes_written == bytes);
    }

    // Always update position after any write operation in case file pointer position changed
    mPosition = ftell(mFile);

    return success;
}

BOOL LLFileSystem::seek(S32 offset, S32 origin)
{
    if (-1 == origin)
    {
        origin = mPosition;
    }

    S32 new_pos = origin + offset;

    S32 size = getSize();

    if (new_pos > size)
    {
        LL_WARNS() << "Attempt to seek past end of file" << LL_ENDL;

        mPosition = size;
        return FALSE;
    }
    else if (new_pos < 0)
    {
        LL_WARNS() << "Attempt to seek past beginning of file" << LL_ENDL;

        mPosition = 0;
        return FALSE;
    }

    mPosition = new_pos;
    return TRUE;
}

S32 LLFileSystem::tell() const
{
    return mPosition;
}

S32 LLFileSystem::getSize() const
{
    S32 file_size = 0;

    llstat stat;
	if (LLFile::stat(mFilePath, &stat) == 0)
	{
		file_size = stat.st_size;
	}

    return file_size;
}

S32 LLFileSystem::getMaxSize()
{
    // Max size at 50mb to avoid error conditions resulting in huge files on disk
    return 52428800;
}

BOOL LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    close();

#if LL_WINDOWS
    boost::filesystem::path new_filename = ll_convert_string_to_wide(LLDiskCache::metaDataToFilepath(new_id, new_type));
#else
    boost::filesystem::path new_filename = LLDiskCache::metaDataToFilepath(new_id, new_type);
#endif

    // Rename needs the new file to not exist.
    LLFile::remove(new_filename, ENOENT);

    if (LLFile::rename(mFilePath, new_filename) != 0)
    {
        // We would like to return FALSE here indicating the operation
        // failed but the original code does not and doing so seems to
        // break a lot of things so we go with the flow...
        //return FALSE;
        LL_WARNS() << "Failed to rename " << mFileID << " to " << new_id << " reason: " << strerror(errno) << LL_ENDL;
    }

    mFileID = new_id;
    mFileType = new_type;

    mFilePath = std::move(new_filename);

    return TRUE;
}

BOOL LLFileSystem::remove()
{
    close();

    LLFile::remove(mFilePath, ENOENT);

    return TRUE;
}

BOOL LLFileSystem::exists() const
{
    llstat stat;
    if (LLFile::stat(mFilePath, &stat) == 0)
    {
        return S_ISREG(stat.st_mode) && stat.st_size > 0;
    }
    return false;
}

void LLFileSystem::updateFileAccessTime()
{
    boost::system::error_code ec;
    /**
     * Threshold in time_t units that is used to decide if the last access time
     * time of the file is updated or not. Added as a precaution for the concern
     * outlined in SL-14582  about frequent writes on older SSDs reducing their
     * lifespan. I think this is the right place for the threshold value - rather
     * than it being a pref - do comment on that Jira if you disagree...
     *
     * Let's start with 1 hour in time_t units and see how that unfolds
     */
    const std::time_t time_threshold = 1 * 60 * 60;

    // current time
    const std::time_t cur_time = std::time(nullptr);

    // file last write time
    const std::time_t last_write_time = boost::filesystem::last_write_time(mFilePath, ec);
    if (ec.failed())
    {
        LL_WARNS() << "Failed to read last write time for cache file " << mFilePath << ": " << ec.message() << LL_ENDL;
        return;
    }

    // delta between cur time and last time the file was written
    const std::time_t delta_time = cur_time - last_write_time;

    // we only write the new value if the time in time_threshold has elapsed
    // before the last one
    if (delta_time > time_threshold)
    {
        boost::filesystem::last_write_time(mFilePath, cur_time, ec);
		if (ec.failed())
		{
			LL_WARNS() << "Failed to update last write time for cache file " << mFilePath << ": " << ec.message() << LL_ENDL;
		}
    }
}

// static
bool LLFileSystem::getExists(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    LLFileSystem file(file_id, file_type, READ);
    return file.exists();
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    LLFileSystem file(file_id, file_type, READ_WRITE);
    return file.remove();
}

// static
bool LLFileSystem::renameFile(const LLUUID& old_file_id, const LLAssetType::EType old_file_type,
    const LLUUID& new_file_id, const LLAssetType::EType new_file_type)
{
    LLFileSystem file(old_file_id, old_file_type, READ_WRITE);
    return file.rename(new_file_id, new_file_type);
}