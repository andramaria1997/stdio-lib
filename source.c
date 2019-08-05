#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

#include "so_stdio.h"

#define BUFSIZE 4096
#define MODE 0666
#define READ 1
#define WRITE 2
#define SEEK 3
#define PIPE_READ	0
#define PIPE_WRITE	1

typedef struct _so_file {
	int fd;
	unsigned char buffer[BUFSIZE];
	long file_pos;
	int buf_data;
	int buf_pos;
	int last_op;
	int errno;
	int feof;
} SO_FILE;

FUNC_DECL_PREFIX SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *f;
	int fd, file_pos = 0;

	if (strcmp(mode, "r") == 0)
		fd = open(pathname, O_RDONLY);
	else if (strcmp(mode, "r+") == 0)
		fd = open(pathname, O_RDWR);
	else if (strcmp(mode, "w") == 0)
		fd = open(pathname, O_WRONLY | O_TRUNC | O_CREAT, MODE);
	else if (strcmp(mode, "w+") == 0)
		fd = open(pathname, O_RDWR | O_TRUNC | O_CREAT, MODE);
	else if (strcmp(mode, "a") == 0) {
		fd = open(pathname, O_WRONLY | O_APPEND | O_CREAT, MODE);
		file_pos = lseek(fd, 0, SEEK_END);
	} else if (strcmp(mode, "a+") == 0) {
		fd = open(pathname, O_RDWR | O_APPEND | O_CREAT, MODE);
		file_pos = lseek(fd, 0, SEEK_END);
	} else
		return NULL;

	if (fd < 0)
		return NULL;

	f = (SO_FILE*)malloc(sizeof(SO_FILE));

	if (f == NULL) {
		close(fd);
		return NULL;
	}

	f->fd = fd;
	f->file_pos = file_pos;
	f->buf_data = 0;
	f->buf_pos = 0;
	f->last_op = 0;
	f->errno = 0;
	f->feof = 0;
	memset(f->buffer, 0, BUFSIZE);

	return f;
}

FUNC_DECL_PREFIX int so_fclose(SO_FILE *stream)
{
	int ret = 0;

	if (so_fflush(stream) != 0)
		ret = SO_EOF;

	close(stream->fd);

	free(stream);

	return ret;
}

FUNC_DECL_PREFIX int so_fileno(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	return stream->fd;
}

FUNC_DECL_PREFIX int so_fflush(SO_FILE *stream)
{
	int rc = 0, written = 0;

	if (stream == NULL)
		return SO_EOF;

	if (stream->last_op == WRITE) {
		do {
			rc = write(stream->fd, stream->buffer + written,
					stream->buf_pos - written);
			written += rc;
		} while ((rc > 0) && (rc < stream->buf_pos - written));

		stream->buf_pos = 0;
		if (rc < 0) {
			stream->errno = SO_EOF;
			return SO_EOF;
		}
	}

	return 0;
}

FUNC_DECL_PREFIX long so_ftell(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	return stream->file_pos;
}

FUNC_DECL_PREFIX int so_fgetc(SO_FILE *stream)
{
	int rc;

	if (stream == NULL)
		return SO_EOF;

	if (stream->last_op == WRITE)
		so_fflush(stream);

	stream->last_op = READ;

	if (stream->buf_pos == BUFSIZE)
		stream->buf_pos = 0;

	if (stream->buf_pos == 0) {
		rc = read(stream->fd, stream->buffer, BUFSIZE);
		if (rc <= 0) {
			stream->feof = 1;
			return SO_EOF;
		}
	}

	stream->file_pos++;

	if (stream->buffer[stream->buf_pos] == 0) {
		stream->feof = 1;
		return -1;
	}

	return stream->buffer[stream->buf_pos++];
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int left = 0, total = nmemb*size, written = 0, rc = BUFSIZE;

	if (stream == NULL)
		return 0;

	if (stream->last_op == WRITE) {
		stream->buf_data = 0;
		so_fflush(stream);
	}

	stream->last_op = READ;

	while ((total > 0) && (!so_feof(stream))) {

		/* left space in stdio buffer */
		left = stream->buf_data - stream->buf_pos;

		/* in case of empty or full buffer, read
		 * from stream's file descriptor
		 */
		if (left == 0) {
			stream->buf_pos = 0;
			stream->buf_data = 0;
			do {
				rc = read(stream->fd,
					stream->buffer + stream->buf_data,
					BUFSIZE - stream->buf_data);
				stream->buf_data += rc;

			} while ((rc > 0) &&
			(stream->buf_data < BUFSIZE) &&
			(stream->buf_data < total));

			if (rc <= 0)
				stream->feof = 1;

			if (rc < 0) {
				stream->errno = SO_EOF;
				return 0;
			}

		}

		if (total < left)
			left = total;

		/* move bytes from stdio buffer to user buffer */
		memcpy(ptr + written, stream->buffer + stream->buf_pos, left);
		stream->file_pos += left;
		stream->buf_pos += left;
		written += left;
		total -= left;

	}

	return written/size;
}

FUNC_DECL_PREFIX int so_fputc(int c, SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	if (stream->last_op == READ)
		stream->buf_pos = 0;

	stream->last_op = WRITE;

	if (stream->buf_pos == BUFSIZE)
		so_fflush(stream);

	stream->buffer[stream->buf_pos++] = c;
	stream->file_pos++;

	return c;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int rc = BUFSIZE, total = nmemb*size, written = 0, left = 0;

	if (stream == NULL)
		return SO_EOF;

	if (stream->last_op == READ)
		stream->buf_pos = 0;

	stream->last_op = WRITE;

	while (total > 0) {

		left = BUFSIZE - stream->buf_pos;

		if (left == 0) {
			so_fflush(stream);
			left = BUFSIZE;
		}

		if (total < left)
			rc = total;
		else
			rc = left;

		memcpy(stream->buffer + stream->buf_pos, ptr + written, rc);

		written += rc;
		stream->buf_pos += rc;
		stream->file_pos += rc;
		total -= rc;

	}

	return written;
}

FUNC_DECL_PREFIX int so_fseek(SO_FILE *stream, long offset, int whence)
{
	if (stream == NULL)
		return SO_EOF;

	if (stream->last_op == WRITE)
		so_fflush(stream);

	if (stream->last_op == READ) {
		stream->buf_data = 0;
		stream->buf_pos = 0;
	}

	stream->last_op = SEEK;

	if ((whence == SEEK_CUR) || (whence == SEEK_SET) ||
		(whence == SEEK_END))
		stream->file_pos += lseek(stream->fd, offset, whence);
	else
		return SO_EOF;

	return 0;
}

FUNC_DECL_PREFIX int so_feof(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	return (stream->feof && stream->buf_pos >= stream->buf_data);
}

FUNC_DECL_PREFIX int so_ferror(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	return stream->errno;
}

FUNC_DECL_PREFIX SO_FILE *so_popen(const char *command, const char *type)
{
	SO_FILE *f;
	int fds[2], rc, fd_child = 0, fd_parent = 0;
	pid_t pid;

	if (strcmp(type, "r") == 0) {
		fd_parent = PIPE_READ;
		fd_child = PIPE_WRITE;
	} else if (strcmp(type, "w") == 0) {
		fd_child = PIPE_READ;
		fd_parent = PIPE_WRITE;
	} else
		return NULL;

	rc = pipe(fds);

	if (rc != 0)
		return NULL;

	pid = fork();

	switch (pid) {
	case -1:
		/* Fork failed */
		close(fds[fd_child]);
		close(fds[fd_parent]);
		return NULL;
	case 0:
		/* Child process */
		close(fds[fd_parent]);
		dup2(fds[fd_child], fd_child);

		execlp("sh", "sh", "-c", command, (char  *) NULL);
		break;
	default:
		/* Parent process */
		close(fds[fd_child]);

		f = (SO_FILE *)malloc(sizeof(SO_FILE));

		if (f == NULL) {
			close(fds[fd_parent]);
			return NULL;
		}

		f->fd = fds[fd_parent];
		f->file_pos = 0;
		f->buf_data = 0;
		f->buf_pos = 0;
		f->last_op = 0;
		f->errno = 0;
		f->feof = 0;
		memset(f->buffer, 0, BUFSIZE);

		return f;
	}

	return NULL;
}


FUNC_DECL_PREFIX int so_pclose(SO_FILE *stream)
{
	int ret = 0;

	if (so_fflush(stream) != 0)
		ret = SO_EOF;

	wait(NULL);
	close(stream->fd);
	free(stream);

	return ret;
}

