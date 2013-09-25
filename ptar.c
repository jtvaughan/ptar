/*
 * Read and Write Plain Text File Archives
 * Written in 2013 by Jordan Vaughan
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define	WRITE_BLOCKSIZE	4096

#define SIGNATURE "###PTARv1###"

static char linkpath[8192], verbose;
static long openmax;

/* file type IDs */
enum { UNKNOWN, REGULARFILE, DIRECTORY, SYMLINK, CHARDEVICE, BLOCKDEVICE, FIFO, SOCKET };

/* stdout identifying info (from fstat(2)) */
static dev_t stdoutdev;
static ino_t stdoutino;

/* file entry metadata */
static char *fpath;
static int ftype = UNKNOWN;	/* see the enum above */
static size_t fsize;	/* for regular files only */
static char *flinktarget;	/* for symlinks only; 0 if not given */
static long fmajor = -1;	/* for devices only; -1 if not given */
static long fminor = -1;	/* for devices only; -1 if not given */
static char *fusername;	/* 0 if not given */
static char *fgroupname;	/* 0 if not given */
static uid_t fuid;
static gid_t fgid;
static mode_t fmode;
static time_t fmtime;

/* nonzero if specified, 0 otherwise */
static char fsizegiven, fuidgiven, fgidgiven, fmodegiven, fmtimegiven;

char *safe_strdup(const char *s) {
	char *ret;

	if ((ret = strdup(s)) == NULL) {
		(void) fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}
	return ret;
}

long open_max(void) {
	if (openmax == 0) {
		errno = 0;
		if ((openmax = sysconf(_SC_OPEN_MAX)) < 0) {
			if (errno == 0) {
				openmax = 256;
			} else {
				perror("error: couldn't determine maximum number of open files");
				exit(EXIT_FAILURE);
			}
		}
	}
	return openmax;
}

int isvalidkeychar(char c) {
	return isalnum(c) || c == ' ' || c == '-' || c == '_';
}

int isvalidkey(const char *start) {
	if (!isalnum(*start)) {
		return 0;
	}
	for (start++; *start != ':' && *start != '\0'; start++) {
		if (!isvalidkeychar(*start)) {
			return 0;
		}
	}
	return 1;
}

int haskey(const char *line) {
	return strchr(line, ':') != NULL && isvalidkey(line);
}

void transformkey(char *start) {
	char *next;
	for (next = start; *start != '\0'; start++, next++) {
		while (isspace(*next) && *next != '\0') {
			next++;
		}
		*start = tolower(*next);
	}
}

char *trim(char *str) {
	size_t len;

	while (isspace(*str)) {
		str++;
	}
	if (*str != '\0') {
		len = strlen(str);
		for (len--; isspace(str[len]); len--) {
			str[len] = '\0';
		}
	}
	return str;
}

int parsemetadata(char *line, char **key, char **value) {
	char *colon;

	colon = strchr(line, ':');
	if (colon && isvalidkey(line)) {
		*colon = '\0';
		*value = trim(colon + 1);
		transformkey(line);
		*key = line;
		return 0;
	}
	colon = trim(line);
	if (*colon != '\0') {
		*key = NULL;
		*value = colon;
		return 0;
	}
	return 1;
}

void write_error(void) {
	(void) fprintf(stderr, "error: couldn't write to standard output: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
}

void write_line(const char *line) {
	if (fprintf(stdout, "%s\n", line) < 0) {
		write_error();
	}
}

void write_metadata(const char *key, const char *value) {
	if (fprintf(stdout, "%s:\t%s\n", key, value) < 0) {
		write_error();
	}
}

void write_numeric_metadata(const char *key, unsigned long long value) {
	if (fprintf(stdout, "%s:\t%llu\n", key, value) < 0) {
		write_error();
	}
}

void write_octal_metadata(const char *key, unsigned int value) {
	if (fprintf(stdout, "%s:\t%.7o\n", key, value) < 0) {
		write_error();
	}
}

void write_blank(void) {
	if (fputc('\n', stdout) == EOF) {
		write_error();
	}
}

void write_divider(void) {
	if (fputs("---\n", stdout) == EOF) {
		write_error();
	}
}

int add_file(const char *fname, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
	FILE *fp;
	struct passwd *passwordinfo;
	struct group *groupinfo;
	char buffer[WRITE_BLOCKSIZE];
	size_t numread;

	/* skip the file if it's the same as stdout (avoids infinite loops) */
	if (sb->st_dev == stdoutdev && sb->st_ino == stdoutino) {
		return 0;
	}

	if (verbose) {
		if (fprintf(stderr, "%s\n", fname) < 0) {
			perror("stderr");
			return 1;
		}
	}
	if ((passwordinfo = getpwuid(sb->st_uid)) == NULL || (groupinfo = getgrgid(sb->st_gid)) == NULL) {
		perror(fname);
		return 1;
	}
	fp = NULL;
	write_blank();
	write_metadata("Path", fname);
	if (S_ISREG(sb->st_mode)) {
		write_metadata("Type", "Regular File");
		write_numeric_metadata("File Size", sb->st_size);
		if ((fp = fopen(fname, "r")) == NULL) {
			perror(fname);
			return 1;
		}
	} else if (S_ISDIR(sb->st_mode)) {
		write_metadata("Type", "Directory");
	} else if (S_ISLNK(sb->st_mode)) {
		write_metadata("Type", "Symbolic Link");
		if (readlink(fname, linkpath, sizeof (linkpath) - 1) == -1) {
			perror(fname);
			return 1;
		}
		write_metadata("Link Target", linkpath);
	} else if (S_ISCHR(sb->st_mode)) {
		write_metadata("Type", "Character Device");
		write_numeric_metadata("Major", major(sb->st_rdev));
		write_numeric_metadata("Minor", minor(sb->st_rdev));
	} else if (S_ISBLK(sb->st_mode)) {
		write_metadata("Type", "Block Device");
		write_numeric_metadata("Major", major(sb->st_rdev));
		write_numeric_metadata("Minor", minor(sb->st_rdev));
	} else if (S_ISFIFO(sb->st_mode)) {
		write_metadata("Type", "FIFO");
	} else if (S_ISSOCK(sb->st_mode)) {
		write_metadata("Type", "Socket");
	} else {
		(void) fprintf(stderr, "%s: illegal file type\n", fname);
		return 1;
	}
	write_metadata("User Name", passwordinfo->pw_name);
	write_numeric_metadata("User ID", sb->st_uid);
	write_metadata("Group Name", groupinfo->gr_name);
	write_numeric_metadata("Group ID", sb->st_gid);
	write_octal_metadata("Permissions", sb->st_mode & ~S_IFMT);
	write_numeric_metadata("Modification Time", sb->st_mtime);
	if (fp) {
		write_divider();
		while (!feof(fp)) {
			numread = fread(buffer, 1, sizeof (buffer), fp);
			if (ferror(fp)) {
				perror(fname);
				return 1;
			}
			if (fwrite(buffer, 1, numread, stdout) != numread) {
				perror(fname);
				fclose(fp);
				return 1;
			}
		}
		fclose(fp);
		write_divider();
	}
	return 0;
}

int archive_file(const char *fname) {
	struct stat sb;

	if (lstat(fname, &sb) != 0) {
		perror(fname);
		return 1;
	} else if (S_ISDIR(sb.st_mode)) {
		return nftw(fname, add_file, open_max(), FTW_PHYS);
	} else {
		return add_file(fname, &sb, 0, NULL);
	}
	return 0;
}

int handle_metadata(size_t lineno, char *key, char *value) {
	char *end;

	if (*value == '\0') {
		(void) fprintf(stderr, "stdin:%zu: empty metadata values are not allowed\n", lineno);
		return 1;
	}
	if (strcmp(key, "path") == 0) {
		if (fpath) {
			(void) fprintf(stderr, "stdin:%zu: file path already specified\n", lineno);
			return 1;
		}
		fpath = safe_strdup(value);
	} else if (strcmp(key, "type") == 0) {
		if (ftype != UNKNOWN) {
			(void) fprintf(stderr, "stdin:%zu: file type already specified\n", lineno);
			return 1;
		}
		transformkey(value);
		if (strcmp(value, "regularfile") == 0) {
			ftype = REGULARFILE;
		} else if (strcmp(value, "directory") == 0) {
			ftype = DIRECTORY;
		} else if (strcmp(value, "symboliclink") == 0) {
			ftype = SYMLINK;
		} else if (strcmp(value, "characterdevice") == 0) {
			ftype = CHARDEVICE;
		} else if (strcmp(value, "blockdevice") == 0) {
			ftype = BLOCKDEVICE;
		} else if (strcmp(value, "fifo") == 0) {
			ftype = FIFO;
		} else if (strcmp(value, "socket") == 0) {
			ftype = SOCKET;
		} else {
			(void) fprintf(stderr, "stdin:%zu: unrecognized file type: %s\n", lineno, value);
			return 1;
		}
	} else if (strcmp(key, "filesize") == 0) {
		if (fsizegiven) {
			(void) fprintf(stderr, "stdin:%zu: file size already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fsize = strtoull(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid file size: %s\n", lineno, value);
			return 1;
		}
		fsizegiven = 1;
	} else if (strcmp(key, "linktarget") == 0) {
		if (flinktarget != NULL) {
			(void) fprintf(stderr, "stdin:%zu: link target already specified\n", lineno);
			return 1;
		}
		flinktarget = safe_strdup(value);
	} else if (strcmp(key, "major") == 0) {
		if (fmajor != -1) {
			(void) fprintf(stderr, "stdin:%zu: major already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fmajor = strtol(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid major: %s\n", lineno, value);
			return 1;
		}
	} else if (strcmp(key, "minor") == 0) {
		if (fminor != -1) {
			(void) fprintf(stderr, "stdin:%zu: file path already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fminor = strtol(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid minor: %s\n", lineno, value);
			return 1;
		}
	} else if (strcmp(key, "username") == 0) {
		if (fusername != NULL) {
			(void) fprintf(stderr, "stdin:%zu: username already specified\n", lineno);
			return 1;
		}
		fusername = safe_strdup(value);
	} else if (strcmp(key, "userid") == 0) {
		if (fuidgiven) {
			(void) fprintf(stderr, "stdin:%zu: uid already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fuid = strtoul(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid uid: %s\n", lineno, value);
			return 1;
		}
		fuidgiven = 1;
	} else if (strcmp(key, "groupname") == 0) {
		if (fgroupname != NULL) {
			(void) fprintf(stderr, "stdin:%zu: groupname already specified\n", lineno);
			return 1;
		}
		fgroupname = safe_strdup(value);
	} else if (strcmp(key, "groupid") == 0) {
		if (fgidgiven) {
			(void) fprintf(stderr, "stdin:%zu: gid already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fgid = strtoul(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid gid: %s\n", lineno, value);
			return 1;
		}
		fgidgiven = 1;
	} else if (strcmp(key, "permissions") == 0) {
		if (fmodegiven) {
			(void) fprintf(stderr, "stdin:%zu: file permissions already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fmode = strtoul(value, &end, 8);
		if (errno != 0 || end == value || *end != '\0' || (fmode & S_IFMT) != 0) {
			(void) fprintf(stderr, "stdin:%zu: invalid file permissions: %s\n", lineno, value);
			return 1;
		}
		fmodegiven = 1;
	} else if (strcmp(key, "modificationtime") == 0) {
		if (fmtimegiven) {
			(void) fprintf(stderr, "stdin:%zu: file modification time already specified\n", lineno);
			return 1;
		}
		errno = 0;
		fmtime = strtoull(value, &end, 10);
		if (errno != 0 || end == value || *end != '\0') {
			(void) fprintf(stderr, "stdin:%zu: invalid file modification time: %s\n", lineno, value);
			return 1;
		}
		fmtimegiven = 1;
	} else {
		(void) fprintf(stderr, "stdin:%zu: unrecognized metadata key name: %s\n", lineno, key);
		return 1;
	}
	return 0;
}

int is_invalid_metadata(void) {
	if (!fpath) {
		return 1;
	}
	switch (ftype) {
	case UNKNOWN:
		return 1;
	case REGULARFILE:
		if (!fsizegiven) {
			return 1;
		}
		break;
	case DIRECTORY:
		break;
	case SYMLINK:
		if (!flinktarget) {
			return 1;
		}
		break;
	case CHARDEVICE:
	case BLOCKDEVICE:
		if (fmajor == -1 || fminor == -1) {
			return 1;
		}
		break;
	case FIFO:
		break;
	case SOCKET:
		break;
	default:
		abort();
		break;
	}
	return !fuidgiven || !fgidgiven || !fusername || !fgroupname || !fmtimegiven || !fmodegiven;
}

void clear_metadata(void) {
	free(fpath);
	fpath = NULL;
	ftype = UNKNOWN;
	free(flinktarget);
	flinktarget = NULL;
	fmajor = -1;
	fminor = -1;
	free(fusername);
	fusername = NULL;
	free(fgroupname);
	fgroupname = NULL;
	fsizegiven = fuidgiven = fgidgiven = fmodegiven = fmtimegiven = 0;
}

/* archive parser states */
enum { SEEKING_METADATA, METADATA, CONTENTS_END };

int scan_archive(int (*onentry)(size_t)) {
	char *line, *key, *value;
	size_t linecap, lineno;
	ssize_t numread;
	int state;

  /* skip to the signature */
	for (line = NULL, lineno = 1; (numread = getline(&line, &linecap, stdin)) != -1; lineno++) {
    if (strcmp(line, SIGNATURE "\n") == 0)
      break;
  }

	/* archive metadata first */
	for (; (numread = getline(&line, &linecap, stdin)) != -1; lineno++) {
		if (parsemetadata(line, &key, &value) != 0) {
			break;
		}
		if (!key && value) {
			(void) fprintf(stderr, "stdin:%zu: illegal archive metadata key-value pair (missing key)\n", lineno);
			return 1;
		}
		if (strcmp(key, "metadataencoding") == 0) {
			transformkey(value);
			if (strcmp(value, "utf-8") != 0 && strcmp(value, "utf8") != 0 && strcmp(value, "ascii") != 0) {
				(void) fprintf(stderr, "stdin:%zu: unrecognized metadata encoding: %s\n", lineno, value);
				return 1;
			}
		} else if (strcmp(key, "extensions") == 0) {
			transformkey(value);
			if (*value != '\0') {
				(void) fprintf(stderr, "stdin:%zu: unrecognized extensions: %s\n", lineno, value);
				return 1;
			}
		} else if (strcmp(key, "archivecreationdate") != 0) {
			(void) fprintf(stderr, "stdin:%zu: unrecognized archive metadata key: %s\n", lineno, key);
			return 1;
		}
	}
	if (ferror(stdin)) {
		(void) fprintf(stderr, "stdin:%zu: %s\n", lineno, strerror(errno));
		return 1;
	} else if (feof(stdin)) {
		return 0;
	}

	/* now for the file entries */
	state = SEEKING_METADATA;
	for (; (numread = getline(&line, &linecap, stdin)) != -1; lineno++) {
		switch (state) {
		case SEEKING_METADATA:
			if (parsemetadata(line, &key, &value) == 0) {
				if (key == NULL) {
					(void) fprintf(stderr, "stdin:%zu: invalid metadata key-value pair (missing key)\n", lineno);
					return 1;
				} else {
					if (handle_metadata(lineno, key, value)) {
						return 1;
					}
					state = METADATA;
				}
			}
			break;
		case METADATA:
			if (parsemetadata(line, &key, &value) == 0) {
				if (key) {
					if (handle_metadata(lineno, key, value)) {
						return 1;
					}
				} else {
					if (strcmp(value, "---") == 0) {
						if (ftype != REGULARFILE) {
							(void) fprintf(stderr, "stdin:%zu: file contents marker found for non-regular file\n", lineno);
							return 1;
						} else if (!fsizegiven) {
							(void) fprintf(stderr, "stdin:%zu: file contents marker found but no file size specified\n", lineno);
							return 1;
						} else if (onentry(lineno)) {
							return 1;
						}
						clear_metadata();
						state = CONTENTS_END;
					} else {
						(void) fprintf(stderr, "stdin:%zu: invalid metadata key-value pair (missing key)\n", lineno);
						return 1;
					}
				}
			} else {
				if (ftype == REGULARFILE) {
					(void) fprintf(stderr, "stdin:%zu: end of regular file metadata reached but no file contents\n", lineno);
					return 1;
				} else if (onentry(lineno)) {
					return 1;
				}
				clear_metadata();
				state = SEEKING_METADATA;
			}
			break;
		case CONTENTS_END:
			if (parsemetadata(line, &key, &value) == 0) {
				if (key) {
					(void) fprintf(stderr, "stdin:%zu: unexpected metadata (expected end-of-file-contents marker \"---\")\n", lineno);
					return 1;
				} else if (strcmp(value, "---") != 0) {
					(void) fprintf(stderr, "stdin:%zu: unexpected additional file data found (expected end-of-file contents marker \"---\" after %zu bytes)\n", lineno, fsize);
					return 1;
				}
				state = SEEKING_METADATA;
			} else {
				(void) fprintf(stderr, "stdin:%zu: unexpected additional file data found (expected end-of-file contents marker \"---\" after %zu bytes)\n", lineno, fsize);
				return 1;
			}
			break;
		default:
			abort();
			break;
		}
	}
	if (ferror(stdin)) {
		(void) fprintf(stderr, "stdin:%zu: %s\n", lineno, strerror(errno));
		return 1;
	} else if (state == METADATA) {
		if (ftype == REGULARFILE) {
			(void) fprintf(stderr, "stdin:%zu: end-of-file reached before reading file contents\n", lineno);
			return 1;
		} else if (onentry(lineno)) {
			return 1;
		}
		clear_metadata();
	} else if (state == CONTENTS_END) {
		(void) fprintf(stderr, "stdin:%zu: end-of-file reached while reading file contents\n", lineno);
		return 1;
	}
	return 0;
}

int listfiles(size_t lineno) {
	char buffer[WRITE_BLOCKSIZE];
	size_t numread;
	size_t numleft;

	if (!fpath) {
		(void) fprintf(stderr, "stdin:%zu: found an entry without a path\n", lineno);
		return 1;
	}
	write_line(fpath);
	if (ftype == REGULARFILE) {
		for (numleft = fsize; numleft > 0; ) {
			numread = fread(buffer, 1, numleft < sizeof (buffer) ? numleft : sizeof (buffer), stdin);
			numleft -= numread;
			if (ferror(stdin)) {
				(void) fprintf(stderr, "stdin:%zu: error while reading: %s\n", lineno, strerror(errno));
				return 1;
			} else if (feof(stdin) && numleft > 0) {
				(void) fprintf(stderr, "stdin:%zu: end-of-file reached while reading file contents (bad file size?)\n", lineno);
				return 1;
			}
		}
	}
	return 0;
}

int extract(size_t lineno) {
	char buffer[WRITE_BLOCKSIZE];
	FILE *fp;
	size_t numleft;
	size_t numread;
	struct timespec times[2];
	int result;

	if (is_invalid_metadata()) {
		(void) fprintf(stderr, "stdin:%zu: incomplete file metadata\n", lineno);
		return 1;
	}
	if (verbose) {
		if (fprintf(stderr, "%s\n", fpath) < 0) {
			perror("stderr");
			return 1;
		}
	}
	fp = NULL;
	if (ftype != DIRECTORY && unlink(fpath) != 0 && errno != ENOENT) {
		perror(fpath);
		return 1;
	}
	switch (ftype) {
	case REGULARFILE:
		if ((fp = fopen(fpath, "w")) == NULL) {
			perror(fpath);
			return 1;
		}
		break;
	case DIRECTORY:
		result = mkdir(fpath, fmode);
		break;
	case SYMLINK:
		result = symlink(flinktarget, fpath);
		break;
	case CHARDEVICE:
		result = mknod(fpath, S_IFCHR | fmode, makedev(fmajor, fminor));
		break;
	case BLOCKDEVICE:
		result = mknod(fpath, S_IFBLK | fmode, makedev(fmajor, fminor));
		break;
	case FIFO:
		result = mkfifo(fpath, fmode);
		break;
	case SOCKET:
		result = mknod(fpath, S_IFSOCK | fmode, makedev(fmajor, fminor));
		break;
	default:
		abort();
		break;
	}
	if (fp) {
		for (numleft = fsize; numleft; ) {
			numread = fread(buffer, 1, numleft < sizeof (buffer) ? numleft : sizeof (buffer), stdin);
			numleft -= numread;
			if (ferror(stdin)) {
				(void) fprintf(stderr, "stdin:%zu: error while reading: %s\n", lineno, strerror(errno));
				return 1;
			} else if (feof(stdin) && numleft > 0) {
				(void) fprintf(stderr, "stdin:%zu: end-of-file reached while reading file contents (bad file size?)\n", lineno);
				return 1;
			} else if (fwrite(buffer, 1, numread, fp) != numread) {
				perror(fpath);
				return 1;
			}
		}
		(void) fclose(fp);
		if (chmod(fpath, fmode) != 0) {
			perror(fpath);
			return 1;
		}
	}
	times[0].tv_sec = 0;
	times[0].tv_nsec = UTIME_OMIT;
	times[1].tv_sec = fmtime;
	times[1].tv_nsec = 0;
	if (utimensat(AT_FDCWD, fpath, times, 0) != 0) {
		perror(fpath);
		return 1;
	}
	if (lchown(fpath, fuid, fgid) != 0) {
		perror(fpath);
		return 1;
	}
	return 0;
}

void help(void) {
	(void) fprintf(stdout,
"Usage: ptar [-h] [OPTION ...] c|x|t [PATH ...]\n\n"

"     Manipulate plain text archives that are similar to traditional tar(1)\n"
"     files but are more human-readable.\n\n"

"Commands:\n\n"

"     c         Create a new archive and print its contents on standard\n"
"               output.  The files whose PATHs are listed on the command\n"
"               line will be added to the archive.\n\n"

"     x         Extract the contents of the archive from standard input\n"
"               and write the contents to the file system relative to\n"
"               the current working directory.\n\n"

"     t         List the PATHs stored in the archive from standard input.\n"
"               This does not verify that file metadata is complete or\n"
"               valid: It only prints Path values.\n\n"

"Options:\n\n"

"     NOTE: Options must precede command letters.\n\n"

"     -h                  synonym for -help\n"
"     -help               Show this help message and exit.\n"
"     -paths-from-stdin   Read PATHs to be archived from standard input, one\n"
"                         PATH per line, after archiving PATHs specified on\n"
"                         the command line.  (This only makes sense for the\n"
"                         'c' command.)\n"
"     -unbuffered         Disable standard output buffering.\n"
"     -verbose            Verbose output: List PATHs added or extracted on\n"
"                         standard error.\n\n");
}

int main(int argc, char **argv) {
	int error, n;
	char *line, pathsfromstdin;
	size_t linecap;
	ssize_t numread;
	time_t now;
	struct tm *nowtm;
	struct stat sb;

	pathsfromstdin = 0;
	for (n = 1; n < argc; n++) {
		if (strcmp(argv[n], "-help") == 0 || strcmp(argv[n], "-h") == 0) {
			help();
			exit(EXIT_SUCCESS);
		}
	}
	if (argc == 1) {
		help();
		exit(EXIT_FAILURE);
	}
	for (n = 1; n < argc; n++) {
		if (strcmp(argv[n], "-paths-from-stdin") == 0) {
			pathsfromstdin = 1;
		} else if (strcmp(argv[n], "-unbuffered") == 0) {
			if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
				(void) fprintf(stderr, "error: unable to disable standard output buffering: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
		} else if (strcmp(argv[n], "-verbose") == 0) {
			verbose = 1;
		} else if (n == argc) {
			(void) fprintf(stderr, "error: no command given (specify -h for help)\n");
			exit(EXIT_FAILURE);
		} else if (strlen(argv[n]) != 1) {
			(void) fprintf(stderr, "error: command must be exactly one of 'c', 'x', or 't'\n");
			exit(EXIT_FAILURE);
		} else {
			break;
		}
	}
	error = 0;
	switch (argv[n][0]) {
	case 'c':
		if (fstat(1, &sb) != 0) {
			(void) fprintf(stderr, "error: couldn't stat standard output: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		stdoutdev = sb.st_dev;
		stdoutino = sb.st_ino;
		now = time(NULL);
		if ((nowtm = localtime(&now)) == NULL) {
			(void) fprintf(stderr, "error: couldn't convert Epoch timestamp to broken-down time representation\n");
			exit(EXIT_FAILURE);
		}
		if (strftime(linkpath, sizeof (linkpath), "%Y-%m-%dT%H:%M:%SZ", nowtm) == 0) {
			(void) fprintf(stderr, "error: current date and time are too large to fit in ptar's internal buffer\n");
			exit(EXIT_FAILURE);
		}
    write_line(SIGNATURE);
		write_metadata("Metadata Encoding", "utf-8");
		write_metadata("Archive Creation Date", linkpath);
		for (n++; !error && n < argc; n++) {
			error = archive_file(argv[n]);
		}
		if (pathsfromstdin) {
			while (!error && (numread = getline(&line, &linecap, stdin)) != -1) {
				if (numread > 0) {
					if (line[numread - 1] == '\n') {
						line[numread - 1] = '\0';
					}
					archive_file(line);
				}
			}
			if (ferror(stdin)) {
				perror("stdin");
				error = 1;
			}
		}
		break;
	case 'x':
		error = scan_archive(extract);
		break;
	case 't':
		error = scan_archive(listfiles);
		break;
	default:
		(void) fprintf(stderr, "error: unrecognized command: %s (must be one of 'c', 'x', or 't')\n", argv[n]);
		error = 1;
	}
	clear_metadata();
	return error ? EXIT_FAILURE : EXIT_SUCCESS;
}

