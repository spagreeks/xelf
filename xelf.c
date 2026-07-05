#define _GNU_SOURCE
#include <features.h>

#ifndef FD_BUFFER_SIZE
#define FD_BUFFER_SIZE (8 * 1024)
#endif

#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

extern char **environ;

const char MAGIC[4] = {0x7f, 'E', 'L', 'F'};

int eexit(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

int createfd(const char *name)
{
	// Natively supported on Linux 3.17+
	return memfd_create(name, MFD_CLOEXEC);
}

ssize_t writen(int fd, const char buf[], int n)
{
	ssize_t i = 0;
	for (ssize_t nw = 0; i < n;)
	{
		nw = write(fd, buf + i, n - i);
		if (nw < 0)
			return i;
		i += nw;
	}
	return i;
}

int closefd(int fd)
{
	if (fd == STDIN_FILENO)
		return -1;
	return close(fd);
}

bool isRegularFile(int fd)
{
	struct stat st;
	if (fstat(fd, &st) == -1)
		return false;
	return S_ISREG(st.st_mode);
}

// Fast chunked magic finder.
// Reads large blocks, handles read boundaries, and extracts the exact ELF offset.
ssize_t findMagicFast(int srcfd, char *buf, ssize_t *buf_len, size_t *elf_start)
{
	ssize_t total_offset = 0;
	int keep = 0;

	while (1)
	{
		ssize_t nr = read(srcfd, buf + keep, FD_BUFFER_SIZE - keep);
		if (nr < 0)
			return eexit("read magic");
		if (nr == 0 && keep < 4)
			return eexit("no magic found");

		*buf_len = keep + nr;

		// Search the current chunk for the ELF header
		for (ssize_t i = 0; i <= *buf_len - 4; i++)
		{
			if (buf[i] == MAGIC[0] && buf[i + 1] == MAGIC[1] &&
					buf[i + 2] == MAGIC[2] && buf[i + 3] == MAGIC[3])
			{
				*elf_start = i;
				return total_offset + i;
			}
		}

		// Not found in this block. Keep the last 3 bytes in case the 4-byte
		// MAGIC sequence is split exactly across the 8KB read boundary.
		if (*buf_len >= 3)
		{
			keep = 3;
			memmove(buf, buf + *buf_len - 3, 3);
			total_offset += *buf_len - 3;
		}
		else
		{
			keep = *buf_len;
		}
	}
}

int main(int argc, char *argv[])
{
	const char *dstfname = "memfd_elf";
	int srcfd, dstfd;
	char buf[FD_BUFFER_SIZE];
	bool using_stdin = false;

	// 1. Determine input source (Precedence fixed)
	if (argc > 1)
	{
		++argv; // shift out the xelf app name
		srcfd = open(argv[0], O_RDONLY | O_CLOEXEC);
		if (srcfd == -1)
			return eexit("open");
		dstfname = argv[0];
	}
	else
	{
		struct stat st;
		if (fstat(STDIN_FILENO, &st) != -1 && (S_ISFIFO(st.st_mode) || S_ISREG(st.st_mode)))
		{
			srcfd = STDIN_FILENO;
			using_stdin = true;
		}
		else
		{
			return eexit("no input provided");
		}
	}

	// 2. Setup execution arguments (argv shift fixed for STDIN)
	char *stdin_argv[] = {(char *)dstfname, NULL};
	char **exec_argv = using_stdin ? stdin_argv : argv;

	// 3. Find the magic bytes efficiently
	ssize_t buf_len = 0;
	size_t elf_start = 0;
	ssize_t magic_offset = findMagicFast(srcfd, buf, &buf_len, &elf_start);

	// 4. Execution routing
	if (magic_offset == 0 && isRegularFile(srcfd))
	{
		closefd(srcfd);
		if (execve(dstfname, exec_argv, environ) == -1)
			return eexit("execve");
		return EXIT_SUCCESS;
	}

	// 5. Memory file creation
	dstfd = createfd(dstfname);
	if (dstfd == -1)
	{
		closefd(srcfd);
		return eexit("createfd");
	}

	// Dump the remaining part of the chunk that contains the ELF header
	ssize_t to_write = buf_len - elf_start;
	if (to_write > 0)
	{
		if (writen(dstfd, buf + elf_start, to_write) < to_write)
			perror("writen chunk");
	}

	// 6. Continue rapidly copying the rest of the file
	for (ssize_t nr = 0;;)
	{
		nr = read(srcfd, buf, FD_BUFFER_SIZE);
		if (nr < 0)
			return eexit("read");
		if (nr == 0)
			break;
		if (writen(dstfd, buf, nr) < nr)
			perror("writen");
	}
	closefd(srcfd);

	if (fsync(dstfd) == -1)
		perror("fsync");

	// 7. Native in-memory execution
	if (fexecve(dstfd, exec_argv, environ) == -1)
	{
		closefd(dstfd);
		return eexit("fexecve");
	}

	return EXIT_SUCCESS;
}