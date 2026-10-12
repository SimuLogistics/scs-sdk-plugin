#include "sharedmemory.hpp"

#include <cstring>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void SharedMemory::LogError(const char* logPtr) {
#ifdef SHAREDMEM_LOGGING
	if (this->logFilePtr == NULL)
	{
		this->logFilePtr = fopen(SHAREDMEM_FILENAME, "a");
	}
	if (this->logFilePtr != NULL)
	{
		fprintf(this->logFilePtr, "%s\r\n", logPtr);
#ifdef _WIN32
		fprintf(this->logFilePtr, "Windows Error code: %d\r\n\r\n", GetLastError());
#else
		fprintf(this->logFilePtr, "errno: %d\r\n\r\n", errno);
#endif
	}
#else
	(void)logPtr;
#endif
}

SharedMemory::SharedMemory(const char* newNamePtr, unsigned int size) {
    this->mapsize = size;
    this->namePtr = newNamePtr;
    this->pBufferPtr = nullptr;
    this->isSharedMemoryHooked = false;
#ifdef SHAREDMEM_LOGGING
	this->logFilePtr = NULL;
#endif

#ifdef _WIN32
    hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, // use paging file
        nullptr, // default security
        PAGE_READWRITE, // read/write access
        0, // maximum object size (high-order DWORD)
        size, // maximum object size (low-order DWORD)
        this->namePtr); // name of mapping object
    LogError("Created file map");
    if (hMapFile == nullptr) {
        LogError("but it's NULL!");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, false, this->namePtr);
            if (hMapFile == nullptr) {
                LogError("Could not open existing file mapping");
                return;
            }
        }
        else {
            LogError("Could not create file mapping object");
            return;
        }
    }
    else {
        LogError("and it's not NULL!");
    }

    this->pBufferPtr = static_cast<void*>(MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (this->pBufferPtr == nullptr) {
        LogError("Could not reserve buffer for shared memory");
        CloseHandle(hMapFile);
        hMapFile = nullptr;
        return;
    }
#else
    // Readable by other local users' processes too, like the Windows mapping in
    // the Local\ namespace is readable by any process of the session.
    fd = shm_open(this->namePtr, O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        LogError("Could not create shared memory object");
        return;
    }
    // macOS only allows sizing a shared memory object once, so skip the
    // ftruncate when a previous run left it at the right size.
    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size != static_cast<off_t>(size)) {
        if (ftruncate(fd, size) == -1) {
            LogError("Could not size shared memory object");
            close(fd);
            fd = -1;
            return;
        }
    }
    void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        LogError("Could not map shared memory");
        close(fd);
        fd = -1;
        return;
    }
    this->pBufferPtr = mapped;
#endif

    memset(this->pBufferPtr, 0, size);
    this->isSharedMemoryHooked = true;
    LogError("Opened MMF");
}


void SharedMemory::Close() {
#ifdef SHAREDMEM_LOGGING
		if (logFilePtr != NULL)
		{
			fclose(logFilePtr);
			logFilePtr = NULL;
		}
#endif
    if (isSharedMemoryHooked) {
#ifdef _WIN32
        if (pBufferPtr != nullptr) UnmapViewOfFile(pBufferPtr);
        if (hMapFile != nullptr) CloseHandle(hMapFile);
#else
        if (pBufferPtr != nullptr) munmap(pBufferPtr, mapsize);
        if (fd != -1) close(fd);
        // Unlike a Windows mapping, a POSIX object outlives its creator, so
        // remove the name: readers see the game is gone and the next start
        // recreates it clean.
        shm_unlink(namePtr);
#endif
    }

    isSharedMemoryHooked = false;

}
