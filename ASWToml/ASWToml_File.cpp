/* **************************************************************************
ASWToml_File.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWToml_File.h"
//---------------------------------------------------------------------------
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <system_error>
#include <utility>
//---------------------------------------------------------------------------
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif
//---------------------------------------------------------------------------

namespace ASWToml
{

namespace Detail
{

namespace
{

// The size of each read or write call, so a size always fits the APIs' 32-bit (Windows) or ssize_t counts
constexpr std::size_t IoChunkSize = 1024 * 1024;

// How many temporary file names a save tries before it gives up (each name is unique to the process and the save, so
// a clash means a leftover file of an earlier process with the same ID)
constexpr int MaxTempFileAttempts = 100;

// Numbers the temporary files of this process's saves
std::atomic<unsigned long> g_TempFileCounter{ 0 };

/////////////////////////////////////////////////////////////////////////////
// TNativeFile
//
// An open file of the operating system (a Windows HANDLE or a POSIX file descriptor), closed by the destructor. Each
// method returns false on failure and sets 'error' to the system's error code.
/////////////////////////////////////////////////////////////////////////////
class TNativeFile
{
private:
#if defined(_WIN32)
    HANDLE m_Handle = INVALID_HANDLE_VALUE;
#else
    int m_Descriptor = -1;
#endif

public:
    TNativeFile() noexcept = default;
    TNativeFile(TNativeFile const&) = delete;
    ~TNativeFile();

    TNativeFile& operator=(TNativeFile const&) = delete;

    void Close() noexcept;
    bool Close(int& error) noexcept;
    bool CreateNew(std::filesystem::path const& path, int& error) noexcept;
    bool Flush(int& error) noexcept;
    bool GetSize(std::uint64_t& size, int& error) noexcept;
#if !defined(_WIN32)
    int GetDescriptor() const noexcept;
#endif
    bool IsRegularFile(bool& isRegular, int& error) noexcept;
    bool OpenForOverwrite(std::filesystem::path const& path, int& error) noexcept;
    bool OpenForRead(std::filesystem::path const& path, int& error) noexcept;
    bool Read(char* buffer, std::size_t size, std::size_t& readSize, int& error) noexcept;
    bool Truncate(std::uint64_t size, int& error) noexcept;
    bool WriteAll(std::string_view data, int& error) noexcept;
};

//---------------------------------------------------------------------------

bool DeleteFileAt(std::filesystem::path const& path, int& error) noexcept;
TASWTomlResult ExceptionFailure(std::filesystem::path const& path, char const* what) noexcept;
TASWTomlResult Failure(ETomlCode code, std::string const& what, std::filesystem::path const& path,
    std::string const& after, int systemError);
bool IsAccessDeniedError(int error) noexcept;
bool IsExistsError(int error) noexcept;
bool IsNotFoundError(int error) noexcept;
int LastSystemError() noexcept;
ETomlCode OpenFailureCode(int error) noexcept;
unsigned long ProcessId() noexcept;
bool ReplaceWithTemp(std::filesystem::path const& temp, std::filesystem::path const& target, bool targetExists,
    int& error);
std::string SystemErrorText(int error);
std::filesystem::path TempFilePath(std::filesystem::path const& target);
TASWTomlMessage Warning(ETomlCode code, std::string const& what, std::filesystem::path const& path,
    std::string const& after, int systemError);
#if defined(_WIN32)
std::string WideToUtf8(std::wstring const& wide);
#endif
TASWTomlResult WriteAtomically(std::filesystem::path const& target, std::string_view contents, bool targetExists,
    TMessageVector& warnings);
void WriteBackup(std::filesystem::path const& target, TASWTomlSavingOptions const& options,
    TMessageVector& warnings);
TASWTomlResult WriteInPlace(std::filesystem::path const& target, std::string_view contents);

//---------------------------------------------------------------------------
/*
    DeleteFileAt

    Deletes the file at 'path'; true if it is gone (or was never there)
*/
bool DeleteFileAt(std::filesystem::path const& path, int& error) noexcept
{
#if defined(_WIN32)
    if (::DeleteFileW(path.c_str()) || ::GetLastError() == ERROR_FILE_NOT_FOUND)
        return true;
#else
    if (::unlink(path.c_str()) == 0 || errno == ENOENT)
        return true;
#endif
    error = LastSystemError();

    return false;
}
//---------------------------------------------------------------------------
/*
    ExceptionFailure

    A result for an exception caught at the boundary of a function that never throws
*/
TASWTomlResult ExceptionFailure(std::filesystem::path const& path, char const* what) noexcept
{
    TASWTomlResult result;
    result.Code = ETomlCode::UnexpectedException;

    try
    {
        result.FilePath = PathToUtf8(path);
        result.Message = std::string("An unexpected exception was caught: ") + what + ".";
    }
    catch (...)
    {
        // Out of memory for the text: the code is still reported
    }

    return result;
}
//---------------------------------------------------------------------------
/*
    Failure

    A failed result: 'what' (e.g. "Could not open the file"), the path in quotes, and the system's description of
    'systemError' when there is one, e.g. Could not open the file "C:\a.toml" for reading: Access is denied.
*/
TASWTomlResult Failure(ETomlCode code, std::string const& what, std::filesystem::path const& path,
    std::string const& after, int systemError)
{
    TASWTomlResult result;
    result.Code = code;
    result.FilePath = PathToUtf8(path);
    result.SystemError = systemError;
    result.Message = what + " \"" + result.FilePath + "\"" + after;

    if (systemError != 0)
        result.Message += ": " + SystemErrorText(systemError);

    result.Message += ".";

    return result;
}
//---------------------------------------------------------------------------
/*
    IsAccessDeniedError

    Whether 'error' means that the operating system denied access
*/
bool IsAccessDeniedError(int error) noexcept
{
#if defined(_WIN32)
    return error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT;
#else
    return error == EACCES || error == EPERM || error == EROFS;
#endif
}
//---------------------------------------------------------------------------
/*
    IsExistsError

    Whether 'error' means that a file created as new already exists
*/
bool IsExistsError(int error) noexcept
{
#if defined(_WIN32)
    return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
#else
    return error == EEXIST;
#endif
}
//---------------------------------------------------------------------------
/*
    IsNotFoundError

    Whether 'error' means that the file or a folder in its path doesn't exist
*/
bool IsNotFoundError(int error) noexcept
{
#if defined(_WIN32)
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
    return error == ENOENT || error == ENOTDIR;
#endif
}
//---------------------------------------------------------------------------
/*
    LastSystemError

    The operating system's last error code (GetLastError() or errno)
*/
int LastSystemError() noexcept
{
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}
//---------------------------------------------------------------------------
/*
    OpenFailureCode

    The code for a file that couldn't be opened because of 'error'
*/
ETomlCode OpenFailureCode(int error) noexcept
{
    if (IsNotFoundError(error))
        return ETomlCode::FileNotFound;

    if (IsAccessDeniedError(error))
        return ETomlCode::AccessDenied;

    return ETomlCode::OpenFailed;
}
//---------------------------------------------------------------------------
/*
    ProcessId

    The ID of this process, for temporary file names
*/
unsigned long ProcessId() noexcept
{
#if defined(_WIN32)
    return static_cast<unsigned long>(::GetCurrentProcessId());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}
//---------------------------------------------------------------------------
/*
    ReplaceWithTemp

    Moves the written temporary file 'temp' over 'target'
*/
bool ReplaceWithTemp(std::filesystem::path const& temp, std::filesystem::path const& target, bool targetExists,
    int& error)
{
#if defined(_WIN32)
    // ReplaceFileW() keeps the target's attributes, security and alternate streams. It can fail where a plain move
    // works (e.g. on some network file systems), and after some failures the target is already gone, so a move
    // follows any failure.
    if (targetExists &&
        ::ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
    {
        return true;
    }

    if (::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;

    error = LastSystemError();

    return false;
#else
    (void)targetExists;

    // The folder whose entry the rename changes, found first: nothing may throw (std::bad_alloc) once the file is
    // replaced, or the save would fail after it succeeded
    std::filesystem::path folder = target.parent_path();
    if (folder.empty())
        folder = ".";

    if (::rename(temp.c_str(), target.c_str()) != 0)
    {
        error = LastSystemError();
        return false;
    }

    // The rename is only durable once the folder's entry is on the disk. A failure here isn't reported: the file is
    // already replaced.
    int descriptor;
    do
    {
        descriptor = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);

    if (descriptor >= 0)
    {
        ::fsync(descriptor);
        ::close(descriptor);
    }

    return true;
#endif
}
//---------------------------------------------------------------------------
/*
    SystemErrorText

    The operating system's description of 'error', without a final period, e.g. "Access is denied"
*/
std::string SystemErrorText(int error)
{
    std::string text;

#if defined(_WIN32)
    wchar_t buffer[512];
    DWORD const size = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(error), 0, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), nullptr);
    if (size > 0)
        text = WideToUtf8(std::wstring(buffer, size));
#else
    text = std::generic_category().message(error);
#endif

    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '.'))
        text.pop_back();

    if (text.empty())
        text = "Unknown error";

    return text;
}
//---------------------------------------------------------------------------
/*
    TempFilePath

    The path 'target' with ".<process ID>.<counter>.tmp" added to its name: a temporary file in the same folder, so
    replacing the target with it is a rename within one file system
*/
std::filesystem::path TempFilePath(std::filesystem::path const& target)
{
    std::string const name = PathToUtf8(target.filename()) + "." + std::to_string(ProcessId()) + "." +
        std::to_string(++g_TempFileCounter) + ".tmp";

    return target.parent_path() / Utf8ToPath(name);
}
//---------------------------------------------------------------------------
/*
    Warning

    A warning for 'warnings', in the same form as Failure()'s message
*/
TASWTomlMessage Warning(ETomlCode code, std::string const& what, std::filesystem::path const& path,
    std::string const& after, int systemError)
{
    TASWTomlResult const failure = Failure(code, what, path, after, systemError);

    TASWTomlMessage message;
    message.Severity = ESeverity::Warning;
    message.Code = code;
    message.Text = failure.Message;
    message.FilePath = failure.FilePath;
    message.SystemError = systemError;

    return message;
}
#if defined(_WIN32)
//---------------------------------------------------------------------------
/*
    WideToUtf8

    'wide' (UTF-16) as UTF-8
*/
std::string WideToUtf8(std::wstring const& wide)
{
    if (wide.empty() || wide.size() > static_cast<std::size_t>(INT_MAX))
        return std::string();

    int const wideSize = static_cast<int>(wide.size());
    int const size = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return std::string();

    std::string utf8(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, &utf8[0], size, nullptr, nullptr);

    return utf8;
}
#endif
//---------------------------------------------------------------------------
/*
    WriteAtomically

    Writes 'contents' to a temporary file next to 'target' and replaces 'target' with it
*/
TASWTomlResult WriteAtomically(std::filesystem::path const& target, std::string_view contents, bool targetExists,
    TMessageVector& warnings)
{
    TNativeFile file;
    std::filesystem::path temp;
    int error = 0;

    for (int attempt = 0; attempt < MaxTempFileAttempts; ++attempt)
    {
        temp = TempFilePath(target);
        error = 0;

        if (file.CreateNew(temp, error) || !IsExistsError(error))
            break;
    }

    if (error != 0)
        return Failure(OpenFailureCode(error), "Could not create the temporary file", temp, "", error);

    // From here on, a failure deletes the temporary file, and so does an exception (std::bad_alloc, e.g. while a
    // failure's message is made, or in ReplaceWithTemp() before the replace), which WriteWholeFile() then returns
    TASWTomlResult result;

    try
    {
        if (!file.WriteAll(contents, error))
        {
            result = Failure(ETomlCode::WriteFailed, "Could not write the temporary file", temp, "", error);
        }
        else
        {
#if !defined(_WIN32)
            // The new file gets the old one's permissions (e.g. 0600 for a private file) and, when the process may
            // set it, its owner, instead of the defaults a new file gets
            struct stat status;
            if (targetExists && ::stat(target.c_str(), &status) == 0)
            {
                ::fchmod(file.GetDescriptor(), status.st_mode & 07777);
                if (::fchown(file.GetDescriptor(), status.st_uid, status.st_gid) != 0)
                {
                    // Only the owner's group, or root, can do this; the permissions above still apply
                }
            }
#endif

            if (!file.Flush(error) || !file.Close(error))
            {
                result = Failure(ETomlCode::WriteFailed, "Could not write the temporary file", temp, "", error);
            }
            else if (!ReplaceWithTemp(temp, target, targetExists, error))
            {
                result = Failure(ETomlCode::ReplaceFailed, "Could not replace the file", target,
                    " with its new contents", error);
            }
        }
    }
    catch (...)
    {
        file.Close();

        // A temporary file that can't be deleted isn't added to the warnings: the exception is what the save returns
        int deleteError = 0;
        static_cast<void>(DeleteFileAt(temp, deleteError));

        throw;
    }

    if (!result.IsOk())
    {
        file.Close();

        int deleteError = 0;
        if (!DeleteFileAt(temp, deleteError))
        {
            warnings.push_back(Warning(ETomlCode::TempFileCleanupFailed, "Could not delete the temporary file", temp,
                " of the failed save", deleteError));
        }
    }

    return result;
}
//---------------------------------------------------------------------------
/*
    WriteBackup

    Copies 'target' to its backup name, adding a warning to 'warnings' if that fails
*/
void WriteBackup(std::filesystem::path const& target, TASWTomlSavingOptions const& options,
    TMessageVector& warnings)
{
    std::filesystem::path const backup = Utf8ToPath(PathToUtf8(target) + options.BackupSuffix);
    std::error_code error;
    std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, error);

    if (error)
    {
        TASWTomlMessage warning = Warning(ETomlCode::BackupFailed, "Could not copy the file to its backup", backup,
            " (the file is saved without a new backup)", 0);
        warning.Text.pop_back();
        warning.Text += ": " + error.message() + ".";
        warning.SystemError = error.value();
        warnings.push_back(warning);
    }
}
//---------------------------------------------------------------------------
/*
    WriteInPlace

    Overwrites 'target' in place with 'contents'
*/
TASWTomlResult WriteInPlace(std::filesystem::path const& target, std::string_view contents)
{
    TNativeFile file;
    int error = 0;

    if (!file.OpenForOverwrite(target, error))
        return Failure(OpenFailureCode(error), "Could not open the file", target, " for writing", error);

    if (!file.WriteAll(contents, error) || !file.Truncate(contents.size(), error) || !file.Flush(error) ||
        !file.Close(error))
    {
        return Failure(ETomlCode::WriteFailed, "Could not write the file", target, "", error);
    }

    return TASWTomlResult();
}
//---------------------------------------------------------------------------


/////////////////////////////////////////////////////////////////////////////
// TNativeFile
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TNativeFile::~TNativeFile()
{
    Close();
}
//---------------------------------------------------------------------------
void TNativeFile::Close() noexcept
{
    int error = 0;
    Close(error);
}
//---------------------------------------------------------------------------
bool TNativeFile::Close(int& error) noexcept
{
#if defined(_WIN32)
    if (m_Handle == INVALID_HANDLE_VALUE)
        return true;

    bool const closed = ::CloseHandle(m_Handle) != 0;
    if (!closed)
        error = LastSystemError();
    m_Handle = INVALID_HANDLE_VALUE;

    return closed;
#else
    if (m_Descriptor < 0)
        return true;

    // A failed close() must not be retried (the descriptor may already be reused), not even after EINTR
    bool const closed = ::close(m_Descriptor) == 0;
    if (!closed)
        error = LastSystemError();
    m_Descriptor = -1;

    return closed;
#endif
}
//---------------------------------------------------------------------------
bool TNativeFile::CreateNew(std::filesystem::path const& path, int& error) noexcept
{
    Close();

#if defined(_WIN32)
    m_Handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_Handle == INVALID_HANDLE_VALUE)
    {
        error = LastSystemError();
        return false;
    }
#else
    do
    {
        m_Descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    } while (m_Descriptor < 0 && errno == EINTR);

    if (m_Descriptor < 0)
    {
        error = LastSystemError();
        return false;
    }
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::Flush(int& error) noexcept
{
#if defined(_WIN32)
    if (!::FlushFileBuffers(m_Handle))
    {
        error = LastSystemError();
        return false;
    }
#else
    int result;
    do
    {
        result = ::fsync(m_Descriptor);
    } while (result != 0 && errno == EINTR);

    if (result != 0)
    {
        error = LastSystemError();
        return false;
    }
#endif

    return true;
}
//---------------------------------------------------------------------------
#if !defined(_WIN32)
int TNativeFile::GetDescriptor() const noexcept
{
    return m_Descriptor;
}
#endif
//---------------------------------------------------------------------------
bool TNativeFile::GetSize(std::uint64_t& size, int& error) noexcept
{
#if defined(_WIN32)
    LARGE_INTEGER fileSize;
    if (!::GetFileSizeEx(m_Handle, &fileSize))
    {
        error = LastSystemError();
        return false;
    }

    size = static_cast<std::uint64_t>(fileSize.QuadPart);
#else
    struct stat status;
    if (::fstat(m_Descriptor, &status) != 0)
    {
        error = LastSystemError();
        return false;
    }

    size = static_cast<std::uint64_t>(status.st_size);
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::IsRegularFile(bool& isRegular, int& error) noexcept
{
#if defined(_WIN32)
    // A folder can't be opened without FILE_FLAG_BACKUP_SEMANTICS, so a disk file is a regular file; a device or a
    // pipe isn't
    DWORD const type = ::GetFileType(m_Handle);
    if (type == FILE_TYPE_UNKNOWN && ::GetLastError() != NO_ERROR)
    {
        error = LastSystemError();
        return false;
    }

    isRegular = type == FILE_TYPE_DISK;
#else
    struct stat status;
    if (::fstat(m_Descriptor, &status) != 0)
    {
        error = LastSystemError();
        return false;
    }

    isRegular = S_ISREG(status.st_mode);
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::OpenForOverwrite(std::filesystem::path const& path, int& error) noexcept
{
    Close();

#if defined(_WIN32)
    // OPEN_ALWAYS rather than CREATE_ALWAYS, which fails for a hidden or system file and resets the attributes
    m_Handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_Handle == INVALID_HANDLE_VALUE)
    {
        error = LastSystemError();
        return false;
    }
#else
    do
    {
        m_Descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    } while (m_Descriptor < 0 && errno == EINTR);

    if (m_Descriptor < 0)
    {
        error = LastSystemError();
        return false;
    }
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::OpenForRead(std::filesystem::path const& path, int& error) noexcept
{
    Close();

#if defined(_WIN32)
    m_Handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (m_Handle == INVALID_HANDLE_VALUE)
    {
        error = LastSystemError();
        return false;
    }
#else
    do
    {
        m_Descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (m_Descriptor < 0 && errno == EINTR);

    if (m_Descriptor < 0)
    {
        error = LastSystemError();
        return false;
    }
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::Read(char* buffer, std::size_t size, std::size_t& readSize, int& error) noexcept
{
    std::size_t const chunkSize = size < IoChunkSize ? size : IoChunkSize;

#if defined(_WIN32)
    DWORD bytesRead = 0;
    if (!::ReadFile(m_Handle, buffer, static_cast<DWORD>(chunkSize), &bytesRead, nullptr))
    {
        error = LastSystemError();
        return false;
    }

    readSize = bytesRead;
#else
    ssize_t bytesRead;
    do
    {
        bytesRead = ::read(m_Descriptor, buffer, chunkSize);
    } while (bytesRead < 0 && errno == EINTR);

    if (bytesRead < 0)
    {
        error = LastSystemError();
        return false;
    }

    readSize = static_cast<std::size_t>(bytesRead);
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::Truncate(std::uint64_t size, int& error) noexcept
{
#if defined(_WIN32)
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFilePointerEx(m_Handle, position, nullptr, FILE_BEGIN) || !::SetEndOfFile(m_Handle))
    {
        error = LastSystemError();
        return false;
    }
#else
    int result;
    do
    {
        result = ::ftruncate(m_Descriptor, static_cast<off_t>(size));
    } while (result != 0 && errno == EINTR);

    if (result != 0)
    {
        error = LastSystemError();
        return false;
    }
#endif

    return true;
}
//---------------------------------------------------------------------------
bool TNativeFile::WriteAll(std::string_view data, int& error) noexcept
{
    while (!data.empty())
    {
        std::size_t const chunkSize = data.size() < IoChunkSize ? data.size() : IoChunkSize;
        std::size_t written;

#if defined(_WIN32)
        DWORD bytesWritten = 0;
        if (!::WriteFile(m_Handle, data.data(), static_cast<DWORD>(chunkSize), &bytesWritten, nullptr))
        {
            error = LastSystemError();
            return false;
        }

        written = bytesWritten;
#else
        ssize_t bytesWritten;
        do
        {
            bytesWritten = ::write(m_Descriptor, data.data(), chunkSize);
        } while (bytesWritten < 0 && errno == EINTR);

        if (bytesWritten < 0)
        {
            error = LastSystemError();
            return false;
        }

        written = static_cast<std::size_t>(bytesWritten);
#endif

        if (written == 0)
        {
            // Nothing written without an error (e.g. a full disk on some systems): stop rather than loop forever
#if defined(_WIN32)
            error = ERROR_DISK_FULL;
#else
            error = ENOSPC;
#endif
            return false;
        }

        data.remove_prefix(written);
    }

    return true;
}
//---------------------------------------------------------------------------

} // namespace

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
TASWTomlResult ReadWholeFile(std::filesystem::path const& path, std::size_t maxSize, std::string& contents) noexcept
{
    try
    {
        contents.clear();

        if (path.empty())
        {
            TASWTomlResult result;
            result.Code = ETomlCode::InvalidArgument;
            result.Message = "The file path is empty.";

            return result;
        }

        TNativeFile file;
        int error = 0;
        bool isRegular = false;

        if (!file.OpenForRead(path, error))
            return Failure(OpenFailureCode(error), "Could not open the file", path, " for reading", error);

        if (!file.IsRegularFile(isRegular, error))
            return Failure(ETomlCode::ReadFailed, "Could not read the file", path, "", error);

        if (!isRegular)
            return Failure(ETomlCode::OpenFailed, "Could not read", path, ", which isn't a file", 0);

        std::uint64_t size = 0;
        if (!file.GetSize(size, error))
            return Failure(ETomlCode::ReadFailed, "Could not read the file", path, "", error);

        std::string const limit = " (" + std::to_string(size) + " bytes) is larger than the largest file loaded (" +
            std::to_string(maxSize) + " bytes)";
        if (size > maxSize)
            return Failure(ETomlCode::FileTooLarge, "The file", path, limit, 0);

        // Read until the end of the file, whose size may have changed since it was checked
        contents.reserve(static_cast<std::size_t>(size));
        char buffer[64 * 1024];
        for (;;)
        {
            std::size_t readSize = 0;
            if (!file.Read(buffer, sizeof(buffer), readSize, error))
            {
                contents.clear();
                return Failure(ETomlCode::ReadFailed, "Could not read the file", path, "", error);
            }

            if (readSize == 0)
                break;

            if (readSize > maxSize - contents.size())
            {
                contents.clear();

                return Failure(ETomlCode::FileTooLarge, "The file", path, " grew larger than the largest file loaded (" +
                    std::to_string(maxSize) + " bytes) while it was read", 0);
            }

            contents.append(buffer, readSize);
        }

        return TASWTomlResult();
    }
    catch (std::exception const& exception)
    {
        contents.clear();
        return ExceptionFailure(path, exception.what());
    }
    catch (...)
    {
        contents.clear();
        return ExceptionFailure(path, "unknown exception");
    }
}
//---------------------------------------------------------------------------
TASWTomlResult WriteWholeFile(std::filesystem::path const& path, std::string_view contents,
    TASWTomlSavingOptions const& options, TMessageVector& warnings) noexcept
{
    try
    {
        if (path.empty())
        {
            TASWTomlResult result;
            result.Code = ETomlCode::InvalidArgument;
            result.Message = "The file path is empty.";
            return result;
        }

        std::filesystem::path target = path;
        std::error_code error;

#if !defined(_WIN32)
        // Replace the file a symbolic link points to, so the link stays. A broken link is replaced by the file.
        if (options.AtomicSave && std::filesystem::is_symlink(target, error))
        {
            std::filesystem::path const resolved = std::filesystem::canonical(target, error);
            if (!error)
                target = resolved;
        }
#endif

        std::filesystem::path const folder = target.parent_path();
        if (options.CreateFolders && !folder.empty() && !std::filesystem::exists(folder, error))
        {
            std::filesystem::create_directories(folder, error);
            if (error)
            {
                TASWTomlResult result = Failure(ETomlCode::CreateFolderFailed, "Could not create the folder", folder,
                    "", 0);
                result.Message.pop_back();
                result.Message += ": " + error.message() + ".";
                result.SystemError = error.value();

                return result;
            }
        }

        bool const targetExists = std::filesystem::exists(target, error);

        if (options.Backup && targetExists)
            WriteBackup(target, options, warnings);

        if (options.AtomicSave)
            return WriteAtomically(target, contents, targetExists, warnings);

        return WriteInPlace(target, contents);
    }
    catch (std::exception const& exception)
    {
        return ExceptionFailure(path, exception.what());
    }
    catch (...)
    {
        return ExceptionFailure(path, "unknown exception");
    }
}
//---------------------------------------------------------------------------

} // namespace Detail

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string PathToUtf8(std::filesystem::path const& path)
{
#if defined(_WIN32)
    return Detail::WideToUtf8(path.wstring());
#else
    return path.string();
#endif
}
//---------------------------------------------------------------------------
std::filesystem::path Utf8ToPath(std::string_view utf8)
{
#if defined(_WIN32)
    if (utf8.empty() || utf8.size() > static_cast<std::size_t>(INT_MAX))
        return std::filesystem::path();

    int const utf8Size = static_cast<int>(utf8.size());
    int const size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8Size, nullptr, 0);
    if (size <= 0)
        return std::filesystem::path();

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8Size, &wide[0], size);

    return std::filesystem::path(wide);
#else
    return std::filesystem::path(std::string(utf8));
#endif
}
//---------------------------------------------------------------------------

} // namespace ASWToml
