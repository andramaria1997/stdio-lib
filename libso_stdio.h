#ifndef LIBSO_STDIO_H
#define LIBSO_STDIO_H

#define BUFSIZE 4096
#define MODE 0666
#define READ 1
#define WRITE 2
#define SEEK 3
#define PIPE_READ	0
#define PIPE_WRITE	1

/**
 * Redefinition of FILE structure
 */

typedef struct _so_file {
	int fd;		/* file descriptor */
	unsigned char buffer[BUFSIZE];	/* read / write buffer */
	long file_pos;	/* file offset from beginning */
	int buf_data;	/* size of data in buffer */
	int buf_pos;	/* buffer read / write position */
	int last_op;	/* last operation made on file (read, write, seek) */
	int errno;	/* error flag; set on error, clear on no error */
	int feof;	/* end of file flag; set when read returns 0 */
} SO_FILE;


#endif /* LIBSO_STDIO_H */
