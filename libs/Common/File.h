////////////////////////////////////////////////////////////////////
// File.h
//
// Copyright 2007 cDc@seacave
// Distributed under the Boost Software License, Version 1.0
// (See http://www.boost.org/LICENSE_1_0.txt)

#ifndef __SEACAVE_FILE_H__
#define __SEACAVE_FILE_H__


// I N C L U D E S /////////////////////////////////////////////////

#include "Streams.h"

// Under both Windows and Unix, the stat function is used for classification

// Under Gnu/Linux, the following classifications are defined
// source: Gnu/Linux man page for stat(2) http://linux.die.net/man/2/stat
//   S_IFMT 	0170000	bitmask for the file type bitfields
//   S_IFSOCK 	0140000	socket (Note this overlaps with S_IFDIR)
//   S_IFLNK 	0120000	symbolic link
//   S_IFREG 	0100000	regular file
//   S_IFBLK 	0060000	block device
//   S_IFDIR 	0040000	directory
//   S_IFCHR 	0020000	character device
//   S_IFIFO 	0010000	FIFO
// There are also some Posix-standard macros:
//   S_ISREG(m)        is it a regular file? 
//   S_ISDIR(m)        directory? 
//   S_ISCHR(m)        character device? 
//   S_ISBLK(m)        block device? 
//   S_ISFIFO(m)       FIFO (named pipe)? 
//   S_ISLNK(m)        symbolic link? (Not in POSIX.1-1996.) 
//   S_ISSOCK(m)       socket? (Not in POSIX.1-1996.)
// Under Windows, the following are defined:
// source: Header file sys/stat.h distributed with Visual Studio 10
//   _S_IFMT  (S_IFMT)   0xF000 file type mask
//   _S_IFREG (S_IFREG)  0x8000 regular
//   _S_IFDIR (S_IFDIR)  0x4000 directory
//   _S_IFCHR (S_IFCHR)  0x2000 character special
//   _S_IFIFO            0x1000 pipe

#ifdef _MSC_VER
#include <io.h>
// file type tests are not defined for some reason on Windows despite them providing the stat() function!
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
// Posix-style macros for Windows
#ifndef STATS
#ifdef _WIN64
#define STATS _stat64
#else
#define STATS stat
#endif
#endif
#ifndef FSTAT
#ifdef _WIN64
#define FSTAT _fstat64
#else
#define FSTAT fstat
#endif
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  ((mode & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode)  ((mode & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISCHR
#define S_ISCHR(mode)  ((mode & _S_IFMT) == _S_IFCHR)
#endif
#ifndef S_ISBLK
#define S_ISBLK(mode)  (false)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(mode) ((mode & _S_IFMT) == _S_IFIFO)
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode)  (false)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(mode) (false)
#endif
#else
#include <unistd.h>
#include <dirent.h>
#ifndef STATS
#if defined(_ENVIRONMENT64) && !defined(__APPLE__)
#define STATS stat64
#else
#define STATS stat
#endif
#endif
#ifndef FSTAT
#if defined(_ENVIRONMENT64) && !defined(__APPLE__)
#define FSTAT fstat64
#else
#define FSTAT fstat
#endif
#endif
#define _taccess access
#endif
#ifdef __APPLE__
#define fdatasync fsync
#endif


// D E F I N E S ///////////////////////////////////////////////////

// size of the stored file size variable
#ifdef LARGEFILESIZE
#define FILESIZE	size_f_t
#else
#define FILESIZE	size_t
#endif

// invalid file handle
#ifdef _MSC_VER
#define FILE_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
#define FILE_INVALID_HANDLE int(-1)
#endif


namespace SEACAVE {

// S T R U C T S ///////////////////////////////////////////////////

class GENERAL_API File : public IOStream {
public:
	typedef struct FILEINFO_TYPE {
		String path;
		FILESIZE size;
		DWORD attrib;
	} FILEINFO;
	typedef cList<FILEINFO> FileInfoArr;

	typedef enum FMCREATE_TYPE {
		OPEN = 0x01,
		CREATE = 0x02,
		TRUNCATE = 0x04
	} FMCREATE;

	typedef enum FMFLAGS_TYPE {
		SYNC = 0x01,
		NOBUFFER = 0x02,
		RANDOM = 0x04,
		SEQUENTIAL = 0x08
	} FMFLAGS;

	inline File() : h(FILE_INVALID_HANDLE) {
		#ifndef _RELEASE
		breakRead = -1;
		breakWrite = -1;
		#endif
	}
	inline File(LPCTSTR aFileName, int access, int mode, int flags=0) : h(FILE_INVALID_HANDLE) {
		#ifndef _RELEASE
		breakRead = -1;
		breakWrite = -1;
		#endif
		File::open(aFileName, access, mode, flags);
	}

	virtual ~File() {
		File::close();
	}

	#ifdef _SUPPORT_CPP11
	inline File(File&& rhs) : h(rhs.h) {
		#ifndef _RELEASE
		breakRead = rhs.breakRead;
		breakWrite = rhs.breakWrite;
		#endif
		rhs.h = FILE_INVALID_HANDLE;
	}

	inline File& operator=(File&& rhs) {
		h = rhs.h;
		#ifndef _RELEASE
		breakRead = rhs.breakRead;
		breakWrite = rhs.breakWrite;
		#endif
		rhs.h = FILE_INVALID_HANDLE;
		return *this;
	}
	#endif

	bool isOpen() const {
		return h != FILE_INVALID_HANDLE;
	}

#ifdef _MSC_VER
	typedef enum FMACCESS_TYPE {
		READ = GENERIC_READ,
		WRITE = GENERIC_WRITE,
		RW = READ | WRITE
	} FMACCESS;

	typedef enum FMCHECKACCESS_TYPE {
		CA_EXIST	= F_OK, // existence
		CA_WRITE	= W_OK, // write
		CA_READ		= R_OK, // read
		CA_RW		= R_OK | W_OK,
	} FMCHECKACCESS;

	/**
	 * Open the file specified.
	 * If there are errors, h is set to FILE_INVALID_HANDLE.
	 * Use isOpen() to check.
	 */
	virtual void open(LPCTSTR aFileName, int access, int mode, int flags=0) {
		ASSERT(access == WRITE || access == READ || access == (READ | WRITE));

		close();

		DWORD m = 0;
		if (mode & OPEN) {
			if (mode & CREATE) {
				m = (mode & TRUNCATE) ? CREATE_ALWAYS : OPEN_ALWAYS;
			} else {
				m = (mode & TRUNCATE) ? TRUNCATE_EXISTING : OPEN_EXISTING;
			}
		} else {
			ASSERT(mode & CREATE);
			m = (mode & TRUNCATE) ? CREATE_ALWAYS : CREATE_NEW;
		}

		DWORD f = 0;
		if (flags & SYNC)
			f |= FILE_FLAG_WRITE_THROUGH;
		if (flags & NOBUFFER)
			f |= FILE_FLAG_NO_BUFFERING;
		if (flags & RANDOM)
			f |= FILE_FLAG_RANDOM_ACCESS;
		if (flags & SEQUENTIAL)
			f |= FILE_FLAG_SEQUENTIAL_SCAN;

		h = ::CreateFile(aFileName, access, FILE_SHARE_READ, NULL, m, f, NULL);
	}

	virtual void close() {
		if (isOpen()) {
			FlushFileBuffers(h);
			CloseHandle(h);
			h = FILE_INVALID_HANDLE;
		}
	}

	uint32_t getLastModified() {
		ASSERT(isOpen());
		FILETIME f = {0};
		::GetFileTime(h, NULL, NULL, &f);
		return convertTime(&f);
	}

	static uint32_t convertTime(FILETIME* f) {
		SYSTEMTIME s = { 1970, 1, 0, 1, 0, 0, 0, 0 };
		FILETIME f2 = {0};
		if (::SystemTimeToFileTime(&s, &f2)) {
			uint64_t* a = (uint64_t*)f;
			uint64_t* b = (uint64_t*)&f2;
			*a -= *b;
			*a /= (1000LL*1000LL*1000LL/100LL);		// 100ns > s
			return (uint32_t)*a;
		}
		return 0;
	}

	size_f_t getSize() const override {
		ASSERT(isOpen());
		DWORD x;
		DWORD l = ::GetFileSize(h, &x);
		if ((l == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR))
			return SIZE_NA;
		return (size_f_t)l | ((size_f_t)x)<<32;
	}

	virtual bool setSize(size_f_t newSize) {
		const size_f_t pos = getPos();
		if (pos == SIZE_NA)
			return false;
		if (!setPos(newSize))
			return false;
		if (!setEOF())
			return false;
		if (!setPos(pos))
			return false;
		return true;
	}

	size_f_t getPos() const override {
		ASSERT(isOpen());
		LONG x = 0;
		const DWORD l = ::SetFilePointer(h, 0, &x, FILE_CURRENT);
		if (l == INVALID_SET_FILE_POINTER)
			return SIZE_NA;
		return (size_f_t)l | ((size_f_t)x)<<32;
	}

	bool setPos(size_f_t pos) override {
		ASSERT(isOpen());
		LONG x = (LONG) (pos>>32);
		return (::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_BEGIN) != INVALID_SET_FILE_POINTER);
	}

	virtual bool setEndPos(size_f_t pos) {
		ASSERT(isOpen());
		LONG x = (LONG) (pos>>32);
		return (::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_END) != INVALID_SET_FILE_POINTER);
	}

	virtual bool movePos(size_f_t pos) {
		ASSERT(isOpen());
		LONG x = (LONG) (pos>>32);
		return (::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_CURRENT) != INVALID_SET_FILE_POINTER);
	}

	size_t read(void* buf, size_t len) override {
		ASSERT(isOpen());
		#ifndef _RELEASE
		if (breakRead != (size_t)(-1)) {
			if (breakRead <= len) {
				ASSERT("FILE::read() break" == NULL);
				breakRead = -1;
			} else {
				breakRead -= len;
			}
		}
		#endif
		DWORD x;
		if (!::ReadFile(h, buf, (DWORD)len, &x, NULL))
			return STREAM_ERROR;
		return x;
	}

	size_t write(const void* buf, size_t len) override {
		ASSERT(isOpen());
		#ifndef _RELEASE
		if (breakWrite != (size_t)(-1)) {
			if (breakWrite <= len) {
				ASSERT("FILE::write() break" == NULL);
				breakWrite = -1;
			} else {
				breakWrite -= len;
			}
		}
		#endif
		DWORD x;
		if (!::WriteFile(h, buf, (DWORD)len, &x, NULL))
			return STREAM_ERROR;
		ASSERT(x == len);
		return x;
	}
	virtual bool setEOF() {
		ASSERT(isOpen());
		return (SetEndOfFile(h) != FALSE);
	}

	size_t flush() override {
		ASSERT(isOpen());
		return (FlushFileBuffers(h) ? 0 : STREAM_ERROR);
	}

	virtual bool getInfo(BY_HANDLE_FILE_INFORMATION* fileInfo) {
		ASSERT(isOpen());
		return (GetFileInformationByHandle(h, fileInfo) != FALSE);
	}

	static uint32_t getAttrib(LPCTSTR aFileName) {
		return GetFileAttributes(aFileName);
	}

	static bool setAttrib(LPCTSTR aFileName, uint32_t attribs) {
		return (SetFileAttributes(aFileName, attribs) != FALSE);
	}

	static void deleteFile(LPCTSTR aFileName) { ::DeleteFile(aFileName); }
	static bool renameFile(LPCTSTR source, LPCTSTR target) {
		if (!::MoveFile(source, target)) {
			// Can't move, try copy/delete...
			if (!::CopyFile(source, target, FALSE))
				return false;
			deleteFile(source);
		}
		return true;
	}
	static bool copyFile(LPCTSTR source, LPCTSTR target) { return ::CopyFile(source, target, FALSE) == TRUE; }

	static size_f_t getSize(LPCTSTR aFileName) {
		const HANDLE fh = ::CreateFile(aFileName, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
		if (fh == FILE_INVALID_HANDLE)
			return SIZE_NA;
		DWORD x;
		DWORD l = ::GetFileSize(fh, &x);
		CloseHandle(fh);
		if ((l == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR))
			return SIZE_NA;
		return (((size_f_t)l) | (((size_f_t)x)<<32));
	}


	static size_f_t findFiles(const String& _strPath, const String& strMask, bool bProcessSubdir, FileInfoArr& arrFiles) {
		// List all the files.
		WIN32_FIND_DATA fd;
		HANDLE hFind;
		size_f_t totalSize = 0;
		String strPath(_strPath);
		Util::ensureFolderSlash(strPath);
		//Find all the files in this folder.
		hFind = FindFirstFile((strPath + strMask).c_str(), &fd);
		if (hFind != FILE_INVALID_HANDLE) {
			do {
				// this is a file that can be used
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				// Store the file name.
				FILEINFO& fileInfo = arrFiles.AddEmpty();
				fileInfo.path = strPath + fd.cFileName;
				#ifdef LARGEFILESIZE
				fileInfo.size = (((size_f_t)fd.nFileSizeLow) | (((size_f_t)fd.nFileSizeHigh)<<32));
				#else
				fileInfo.size = fd.nFileSizeLow;
				#endif
				fileInfo.attrib = fd.dwFileAttributes;
				totalSize += fileInfo.size;
			} while (FindNextFile(hFind, &fd));
			FindClose(hFind);
		}
		//Process the subfolders also...
		if (!bProcessSubdir)
			return totalSize;
		hFind = FindFirstFile((strPath + '*').c_str(), &fd);
		if (hFind != FILE_INVALID_HANDLE) {
			do {
				// if SUBDIR then process that too
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					continue;
				if (!_tcscmp(fd.cFileName, _T(".")))
					continue;
				if (!_tcscmp(fd.cFileName, _T("..")))
					continue;
				// Process all subfolders recursively
				totalSize += findFiles(strPath + fd.cFileName + PATH_SEPARATOR, strMask, true, arrFiles);
			} while (FindNextFile(hFind, &fd));
			FindClose(hFind);
		}
		return totalSize;
	}

#else // _MSC_VER

	typedef enum FMACCESS_TYPE {
		READ = 0x01,
		WRITE = 0x02,
		RW = READ | WRITE,
	} FMACCESS;

	typedef enum FMCHECKACCESS_TYPE {
		CA_EXIST	= F_OK, // existence
		CA_WRITE	= W_OK, // write
		CA_READ		= R_OK, // read
		CA_RW		= R_OK | W_OK,
		CA_EXEC		= X_OK, // execute
	} FMCHECKACCESS;

	/**
	 * Open the file specified.
	 * If there are errors, h is set to FILE_INVALID_HANDLE.
	 * Use isOpen() to check.
	 */
	virtual void open(LPCTSTR aFileName, int access, int mode, int flags=0) {
		ASSERT(access == WRITE || access == READ || access == (READ | WRITE));

		close();

		int m = 0;
		if (access == READ)
			m |= O_RDONLY;
		else if (access == WRITE)
			m |= O_WRONLY;
		else
			m |= O_RDWR;

		if (mode & CREATE)
			m |= O_CREAT;
		if (mode & TRUNCATE)
			m |= O_TRUNC;

		if (flags & SYNC)
			m |= O_DSYNC;
		#ifndef __APPLE__
		if (flags & NOBUFFER)
			m |= O_DIRECT;
		#endif
		h = ::open(aFileName, m, S_IRUSR | S_IWUSR);
	}

	virtual void close() {
		if (h != FILE_INVALID_HANDLE) {
			::close(h);
			h = FILE_INVALID_HANDLE;
		}
	}

	uint32_t getLastModified() {
		ASSERT(isOpen());
		struct STATS s;
		if (::FSTAT(h, &s) == -1)
			return 0;
		return (uint32_t)s.st_mtime;
	}

	size_f_t getSize() const override {
		ASSERT(isOpen());
		struct STATS s;
		if (::FSTAT(h, &s) == -1)
			return SIZE_NA;
		return (size_f_t)s.st_size;
	}

	size_f_t getPos() const override {
		ASSERT(isOpen());
		return (size_f_t)lseek(h, 0, SEEK_CUR);
	}

	bool setPos(size_f_t pos) override { ASSERT(isOpen()); return lseek(h, (off_t)pos, SEEK_SET) != (off_t)-1; }
	virtual bool setEndPos(size_f_t pos) { ASSERT(isOpen()); return lseek(h, (off_t)pos, SEEK_END) != (off_t)-1; }
	virtual bool movePos(size_f_t pos) { ASSERT(isOpen()); return lseek(h, (off_t)pos, SEEK_CUR) != (off_t)-1; }

	size_t read(void* buf, size_t len) override {
		ASSERT(isOpen());
		#ifndef _RELEASE
		if (breakRead != (size_t)(-1)) {
			if (breakRead <= len) {
				ASSERT("FILE::read() break" == NULL);
				breakRead = -1;
			} else {
				breakRead -= len;
			}
		}
		#endif
		ssize_t x = ::read(h, buf, len);
		if (x == -1)
			return STREAM_ERROR;
		return (size_t)x;
	}

	size_t write(const void* buf, size_t len) override {
		ASSERT(isOpen());
		#ifndef _RELEASE
		if (breakWrite != (size_t)(-1)) {
			if (breakWrite <= len) {
				ASSERT("FILE::write() break" == NULL);
				breakWrite = -1;
			} else {
				breakWrite -= len;
			}
		}
		#endif
		ssize_t x = ::write(h, buf, len);
		if (x == -1)
			return STREAM_ERROR;
		if (x < (ssize_t)len)
			return STREAM_ERROR;
		return x;
	}

	virtual bool setEOF() {
		ASSERT(isOpen());
		return (ftruncate(h, (off_t)getPos()) != -1);
	}
	virtual bool setSize(size_f_t newSize) {
		ASSERT(isOpen());
		return (ftruncate(h, (off_t)newSize) != -1);
	}

	size_t flush() override {
		ASSERT(isOpen());
		return fdatasync(h);
	}

	static void deleteFile(LPCTSTR aFileName) { ::remove(aFileName); }
	static bool renameFile(LPCTSTR source, LPCTSTR target) { return ::rename(source, target) == 0; }
	static bool copyFile(LPCTSTR source, LPCTSTR target) {
		std::ifstream src(source, std::ios::binary);
		if (!src.is_open())
			return false;
		std::ofstream dst(target, std::ios::binary);
		if (!dst.is_open())
			return false;
		dst << src.rdbuf();
		return true;
	}

	static size_f_t getSize(LPCTSTR aFileName) {
		struct STATS buf;
		if (STATS(aFileName, &buf) != 0)
			return SIZE_NA;
		return buf.st_size;
	}

#endif // _MSC_VER

	// test for whether there's something (i.e. folder or file) with this name
	// and what access mode is supported
	static bool isPresent(LPCTSTR path) {
		struct STATS buf;
		return STATS(path, &buf) == 0;
	}
	static bool access(LPCTSTR path, int mode=CA_EXIST) {
		return ::_taccess(path, mode) == 0;
	}
	// test for whether there's something present and its a folder
	static bool isFolder(LPCTSTR path) {
		struct STATS buf;
		if (!(STATS(path, &buf) == 0))
			return false;
		// If the object is present, see if it is a directory
		// this is the Posix-approved way of testing
		return S_ISDIR(buf.st_mode);
	}
	// test for whether there's something present and its a file
	// a file can be a regular file, a symbolic link, a FIFO or a socket, but not a device
	static bool isFile(LPCTSTR path) {
		struct STATS buf;
		if (!(STATS(path, &buf) == 0))
			return false;
		// If the object is present, see if it is a file or file-like object
		// Note that devices are neither folders nor files
		// this is the Posix-approved way of testing
		return S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode) || S_ISSOCK(buf.st_mode) || S_ISFIFO(buf.st_mode);
	}

	// time the file was originally created
	static time_t getCreated(LPCTSTR path) {
		struct STATS buf;
		if (STATS(path, &buf) != 0) return 0;
		return buf.st_ctime;
	}
	// time the file was last modified
	static time_t getModified(LPCTSTR path) {
		struct STATS buf;
		if (STATS(path, &buf) != 0) return 0;
		return buf.st_mtime;
	}
	// time the file was accessed
	static time_t getAccessed(LPCTSTR path) {
		struct STATS buf;
		if (STATS(path, &buf) != 0) return 0;
		return buf.st_atime;
	}

	// set the current folder
	static bool setCurrentFolder(LPCTSTR path) {
		if (!isFolder(path))
			return false;
		#ifdef _MSC_VER
		// Windows implementation - this returns non-zero for success
		return (SetCurrentDirectory(path) != 0);
		#else
		// Unix implementation - this returns zero for success
		return (chdir(path) == 0);
		#endif
	}

	template <class VECTOR>
	inline size_t write(const VECTOR& arr) {
		const typename VECTOR::IDX nSize(arr.GetSize());
		size_t nBytes(write(&nSize, sizeof(typename VECTOR::IDX)));
		nBytes += write(arr.GetData(), arr.GetDataSize());
		return nBytes;
	}

	template <class VECTOR>
	inline size_t read(VECTOR& arr) {
		typename VECTOR::IDX nSize;
		size_t nBytes(read(&nSize, sizeof(typename VECTOR::IDX)));
		arr.Resize(nSize);
		nBytes += read(arr.GetData(), arr.GetDataSize());
		return nBytes;
	}

	enum { LAYER_ID_IN=3 };
	InputStream* getInputStream(int typ=InputStream::LAYER_ID_IN) override { return (typ == LAYER_ID_IN ? static_cast<InputStream*>(this) : IOStream::getInputStream(typ)); }
	enum { LAYER_ID_OUT=3 };
	OutputStream* getOutputStream(int typ=OutputStream::LAYER_ID_OUT) override { return (typ == LAYER_ID_OUT ? static_cast<OutputStream*>(this) : IOStream::getOutputStream(typ)); }

protected:
	#ifdef _MSC_VER
	HANDLE h;
	#else
	int h;
	#endif

public:
	#ifndef _RELEASE
	size_t breakRead;
	size_t breakWrite;
	#endif

private:
	File(const File&);
	File& operator=(const File&);
};
typedef File* LPFILE;
/*----------------------------------------------------------------*/

} // namespace SEACAVE

#endif // __SEACAVE_FILE_H__