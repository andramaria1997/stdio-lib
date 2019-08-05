#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

#include "so_stdio.h"
#include "libso_stdio.h"

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *f;
	int fd, file_pos = 0;

	/* check if mode is valid and call open with corresponding params */
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

	/* create SO_FILE structure and return it */
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

int so_fclose(SO_FILE *stream)
{
	int ret = 0;

	/* fflush data, close fd and free memory */
	if (so_fflush(stream) != 0)
		ret = SO_EOF;

	close(stream->fd);
	free(stream);

	return ret;
}

int so_fileno(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	/* return file descriptor */
	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	int rc = 0, written = 0;

	if (stream == NULL)
		return SO_EOF;

	/* in case of previous write, write to fd all data from buffer */
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

long so_ftell(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	/* return position in file */
	return stream->file_pos;
}

int so_fgetc(SO_FILE *stream)
{
	int rc;

	if (stream == NULL)
		return SO_EOF;

	/* in case of previous write, fflush and delete data from buffer */
	if (stream->last_op == WRITE)
		so_fflush(stream);

	/* set last op made on file */
	stream->last_op = READ;

	/* in case of full buffer, reset buffer and read from file desc */
	if (stream->buf_pos == stream->buf_data) {
		stream->buf_pos = 0;
		stream->buf_data = 0;
		rc = read(stream->fd, stream->buffer, BUFSIZE);
		stream->buf_data += rc;
		if (rc <= 0) {
			stream->feof = 1;
			return SO_EOF;
		}
	}

	/* increment file cursor position */
	stream->file_pos++;

	/* return char from current bufer position */
	return stream->buffer[stream->buf_pos++];
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int left = 0, total = nmemb*size, written = 0, rc = BUFSIZE;

	if (stream == NULL)
		return 0;

	/* in case of previous write, fflush and delete data from buffer */
	if (stream->last_op == WRITE) {
		stream->buf_data = 0;
		so_fflush(stream);
	}

	/* set last op made on file */
	stream->last_op = READ;

	while ((total > 0) && (!so_feof(stream))) {

		/* left data in stdio buffer */
		left = stream->buf_data - stream->buf_pos;

		/* in case of no data in buffer, read from stream's file desc
		 * and reset buffer
		 */
		if (left == 0) {
			stream->buf_pos = 0;
			stream->buf_data = 0;
			rc = read(stream->fd, stream->buffer, BUFSIZE);
			stream->buf_data += rc;

			/* mark end of file */
			if (rc <= 0)
				stream->feof = 1;

			/* in case of error, set errno and return 0 */
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

	/* return number of elements read */
	return written/size;
}

int so_fputc(int c, SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	/* in case of previous read, reset buffer */
	if (stream->last_op == READ)
		stream->buf_pos = 0;

	/* set last op made on file */
	stream->last_op = WRITE;

	/* in case of full buffer, fflush */
	if (stream->buf_pos == BUFSIZE)
		so_fflush(stream);

	/* put char in SO_FILE buffer and increment file cursor position */
	stream->buffer[stream->buf_pos++] = c;
	stream->file_pos++;

	return c;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int total = nmemb*size, written = 0, left = 0;

	if (stream == NULL)
		return SO_EOF;

	/* in case of previous read, reset buffer */
	if (stream->last_op == READ)
		stream->buf_pos = 0;

	/* set last op made on file */
	stream->last_op = WRITE;

	while (total > 0) {

		left = BUFSIZE - stream->buf_pos;

		/* in case of full buffer, fflush */
		if (left == 0) {
			so_fflush(stream);
			left = BUFSIZE;
		}

		if (total < left)
			left = total;

		/* write data from user buffer (ptr) in SO_FILE buffer */
		memcpy(stream->buffer + stream->buf_pos, ptr + written, left);

		written += left;
		stream->buf_pos += left;
		stream->file_pos += left;
		total -= left;

	}

	/* return number of elements written */
	return written/size;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	if (stream == NULL)
		return SO_EOF;

	/* in case of previous write, fflush */
	if (stream->last_op == WRITE)
		so_fflush(stream);

	/* in case of previous read, reset buffer */
	if (stream->last_op == READ) {
		stream->buf_data = 0;
		stream->buf_pos = 0;
	}

	/* set last op made on file */
	stream->last_op = SEEK;

	/* if whence is valid, move file cursor position */
	if ((whence == SEEK_CUR) || (whence == SEEK_SET) ||
		(whence == SEEK_END))
		stream->file_pos = lseek(stream->fd, offset, whence);
	else
		return SO_EOF;

	return 0;
}

int so_feof(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	/* reach EOF when read gets to EOF (set feof flag) and all data from
	 * buffer is read by user with so_fgetc or so_fread
	 */
	return (stream->feof && stream->buf_pos == stream->buf_data);
}

int so_ferror(SO_FILE *stream)
{
	if (stream == NULL)
		return SO_EOF;

	/* return errno preivously set */
	return stream->errno;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	SO_FILE *f;
	int fds[2], rc, fd_child = 0, fd_parent = 0;
	pid_t pid;

	/* check for read or write */
	if (strcmp(type, "r") == 0) {
		fd_parent = PIPE_READ;
		fd_child = PIPE_WRITE;
	} else if (strcmp(type, "w") == 0) {
		fd_child = PIPE_READ;
		fd_parent = PIPE_WRITE;
	} else
		return NULL;

	/* create pipe */
	rc = pipe(fds);

	if (rc != 0)
		return NULL;

	pid = fork();

	switch (pid) {
	case -1:
		/* fork failed */
		close(fds[fd_child]);
		close(fds[fd_parent]);
		return NULL;
	case 0:
		/* child process */
		close(fds[fd_parent]);

		/* redirect stdin or stdout */
		dup2(fds[fd_child], fd_child);

		/* exec command */
		execlp("sh", "sh", "-c", command, (char  *) NULL);
		break;
	default:
		/* parent process */
		close(fds[fd_child]);

		/* create SO_FILE structure and return it */
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

int so_pclose(SO_FILE *stream)
{
	int ret = 0;

	/* fflush data */
	if (so_fflush(stream) != 0)
		ret = SO_EOF;

	/* wait for child process, close file desc and free memory */
	close(stream->fd);
	wait(NULL);
	free(stream);

	return ret;
}
