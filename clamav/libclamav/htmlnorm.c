/*
 *  Normalise HTML text.
 *  Decode MS Script Encoder protection. 
 *
 *  Copyright (C) 2004 trog@uncon.org
 *
 *  The ScrEnc decoder was initially based upon an analysis by Andreas Marx.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#if HAVE_MMAP
#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#else /* HAVE_SYS_MMAN_H */
#undef HAVE_MMAP
#endif
#endif

#include "others.h"
#include "htmlnorm.h"

#define HTML_STR_LENGTH 1024
#define FALSE (0)
#define TRUE (1)

typedef enum {
    HTML_BAD_STATE,
    HTML_NORM,
    HTML_COMMENT,
    HTML_CHAR_REF,
    HTML_SKIP_WS,
    HTML_TRIM_WS,
    HTML_TAG,
    HTML_TAG_ARG,
    HTML_TAG_ARG_VAL,
    HTML_TAG_ARG_EQUAL,
    HTML_PROCESS_TAG,
    HTML_CHAR_REF_DECODE,
    HTML_SKIP_LENGTH,
    HTML_JSDECODE,
    HTML_JSDECODE_LENGTH,
    HTML_JSDECODE_DECRYPT,
    HTML_SPECIAL_CHAR,
} html_state;

typedef enum {
    SINGLE_QUOTED,
    DOUBLE_QUOTED,
    NOT_QUOTED,
} quoted_state;

typedef struct m_area_tag {
	unsigned char *buffer;
	off_t length;
	off_t offset;
} m_area_t;

#define HTML_FILE_BUFF_LEN 8192

typedef struct file_buff_tag {
	int fd;
	unsigned char buffer[HTML_FILE_BUFF_LEN];
	int length;
} file_buff_t;

static const int base64_chars[256] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
};

int table_order[] = {
       00, 02, 01, 00, 02, 01, 02, 01, 01, 02, 01, 02, 00, 01, 02, 01,
       00, 01, 02, 01, 00, 00, 02, 01, 01, 02, 00, 01, 02, 01, 01, 02,
       00, 00, 01, 02, 01, 02, 01, 00, 01, 00, 00, 02, 01, 00, 01, 02,
       00, 01, 02, 01, 00, 00, 02, 01, 01, 00, 00, 02, 01, 00, 01, 02
};

int decrypt_tables[3][128] = {
      {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x57, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
       0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
       0x2E, 0x47, 0x7A, 0x56, 0x42, 0x6A, 0x2F, 0x26, 0x49, 0x41, 0x34, 0x32, 0x5B, 0x76, 0x72, 0x43,
       0x38, 0x39, 0x70, 0x45, 0x68, 0x71, 0x4F, 0x09, 0x62, 0x44, 0x23, 0x75, 0x3C, 0x7E, 0x3E, 0x5E,
       0xFF, 0x77, 0x4A, 0x61, 0x5D, 0x22, 0x4B, 0x6F, 0x4E, 0x3B, 0x4C, 0x50, 0x67, 0x2A, 0x7D, 0x74,
       0x54, 0x2B, 0x2D, 0x2C, 0x30, 0x6E, 0x6B, 0x66, 0x35, 0x25, 0x21, 0x64, 0x4D, 0x52, 0x63, 0x3F,
       0x7B, 0x78, 0x29, 0x28, 0x73, 0x59, 0x33, 0x7F, 0x6D, 0x55, 0x53, 0x7C, 0x3A, 0x5F, 0x65, 0x46,
       0x58, 0x31, 0x69, 0x6C, 0x5A, 0x48, 0x27, 0x5C, 0x3D, 0x24, 0x79, 0x37, 0x60, 0x51, 0x20, 0x36},

      {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x7B, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
       0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
       0x32, 0x30, 0x21, 0x29, 0x5B, 0x38, 0x33, 0x3D, 0x58, 0x3A, 0x35, 0x65, 0x39, 0x5C, 0x56, 0x73,
       0x66, 0x4E, 0x45, 0x6B, 0x62, 0x59, 0x78, 0x5E, 0x7D, 0x4A, 0x6D, 0x71, 0x3C, 0x60, 0x3E, 0x53,
       0xFF, 0x42, 0x27, 0x48, 0x72, 0x75, 0x31, 0x37, 0x4D, 0x52, 0x22, 0x54, 0x6A, 0x47, 0x64, 0x2D,
       0x20, 0x7F, 0x2E, 0x4C, 0x5D, 0x7E, 0x6C, 0x6F, 0x79, 0x74, 0x43, 0x26, 0x76, 0x25, 0x24, 0x2B,
       0x28, 0x23, 0x41, 0x34, 0x09, 0x2A, 0x44, 0x3F, 0x77, 0x3B, 0x55, 0x69, 0x61, 0x63, 0x50, 0x67,
       0x51, 0x49, 0x4F, 0x46, 0x68, 0x7C, 0x36, 0x70, 0x6E, 0x7A, 0x2F, 0x5F, 0x4B, 0x5A, 0x2C, 0x57},

      {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x6E, 0x0A, 0x0B, 0x0C, 0x06, 0x0E, 0x0F,
       0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
       0x2D, 0x75, 0x52, 0x60, 0x71, 0x5E, 0x49, 0x5C, 0x62, 0x7D, 0x29, 0x36, 0x20, 0x7C, 0x7A, 0x7F,
       0x6B, 0x63, 0x33, 0x2B, 0x68, 0x51, 0x66, 0x76, 0x31, 0x64, 0x54, 0x43, 0x3C, 0x3A, 0x3E, 0x7E,
       0xFF, 0x45, 0x2C, 0x2A, 0x74, 0x27, 0x37, 0x44, 0x79, 0x59, 0x2F, 0x6F, 0x26, 0x72, 0x6A, 0x39,
       0x7B, 0x3F, 0x38, 0x77, 0x67, 0x53, 0x47, 0x34, 0x78, 0x5D, 0x30, 0x23, 0x5A, 0x5B, 0x6C, 0x48,
       0x55, 0x70, 0x69, 0x2E, 0x4C, 0x21, 0x24, 0x4E, 0x50, 0x09, 0x56, 0x73, 0x35, 0x61, 0x4B, 0x58,
       0x3B, 0x57, 0x22, 0x6D, 0x4D, 0x25, 0x28, 0x46, 0x4A, 0x32, 0x41, 0x3D, 0x5F, 0x4F, 0x42, 0x65}
};

static unsigned char *cli_readline(FILE *stream, m_area_t *m_area, unsigned int max_len)
{
	unsigned char *line, *ptr, *start, *end;
	unsigned int line_len, count;

	line = (unsigned char *) cli_malloc(max_len);
	if (!line) {
		return NULL;
	}

	/* Try and use the memory buffer first */
	if (m_area) {
		start = ptr = m_area->buffer + m_area->offset;
		end = m_area->buffer + m_area->length;
		if (start >= end) {
			free(line);
			return NULL;
		}
		line_len = 1;
		while ((ptr < end) && (*ptr != '\n') && (line_len < (max_len-1))) {
			ptr++;
			line_len++;
		}
		if (ptr == end) {
			line_len--;
			memcpy(line, start, line_len);
			line[line_len] = '\0';
		} else if (*ptr == '\n') {
			memcpy(line, start, line_len);
			line[line_len] = '\0';
		} else {
			/* Hit max_len */
			/* Store the current line end and length*/
			count = line_len;
			while (!isspace(*ptr) && (line_len > 1)) {
				ptr--;
				line_len--;
			}
			if (line_len == 1) {
				line_len=count;
			}
			memcpy(line, start, line_len);
			line[line_len] = '\0';
		}
		m_area->offset += line_len;
	} else {
		if (!stream) {
			cli_dbgmsg("No HTML stream\n");
			free(line);
			return NULL;
		}
		if (fgets(line, max_len, stream) == NULL) {
			free(line);
			return NULL;
		}

		line_len=strlen(line);
		if (line_len == 0) {
			free(line);
			return NULL;
		}
		if (line_len == max_len-1) {
			/* didn't find a whole line - rewind to a space*/
			count = 0;
			while (!isspace(line[--line_len])) {
				count--;
				if (line_len == 0) {
					return line;
				}
			}
			fseek(stream, count, SEEK_CUR);
			line[line_len+1] = '\0';
		}
	}
	return line;
}

static void html_output_flush(file_buff_t *fbuff)
{
	if (fbuff && (fbuff->length > 0)) {
		cli_writen(fbuff->fd, fbuff->buffer, fbuff->length);
		fbuff->length = 0;
	}
}

static void html_output_c(file_buff_t *fbuff1, file_buff_t *fbuff2, unsigned char c)
{
	if (fbuff1) {
		if (fbuff1->length == HTML_FILE_BUFF_LEN) {
			html_output_flush(fbuff1);
		}
		fbuff1->buffer[fbuff1->length++] = c;
	}
	if (fbuff2) {
		if (fbuff2->length == HTML_FILE_BUFF_LEN) {
			html_output_flush(fbuff2);
		}
		fbuff2->buffer[fbuff2->length++] = c;
	}
}

static void html_output_str(file_buff_t *fbuff, unsigned char *str, int len)
{
	if (fbuff) {
		if ((fbuff->length + len) >= HTML_FILE_BUFF_LEN) {
			html_output_flush(fbuff);
		}
		if (len >= HTML_FILE_BUFF_LEN) {
			html_output_flush(fbuff);
			cli_writen(fbuff->fd, str, len);
		} else {
			memcpy(fbuff->buffer + fbuff->length, str, len);
			fbuff->length += len;
		}
	}
}

static char *html_tag_arg_value(tag_arguments_t *tags, char *tag)
{
	int i;
	
	for (i=0; i < tags->count; i++) {
		if (strcmp(tags->tag[i], tag) == 0) {
			return tags->value[i];
		}
	}
	return NULL;
}

static void html_tag_arg_set(tag_arguments_t *tags, char *tag, char *value)
{
	int i;
	
	for (i=0; i < tags->count; i++) {
		if (strcmp(tags->tag[i], tag) == 0) {
			free(tags->value[i]);
			tags->value[i] = strdup(value);
			return;
		}
	}
	return;
}
static void html_tag_arg_add(tag_arguments_t *tags,
		unsigned char *tag, unsigned char *value)
{
	int len, i;
	tags->count++;
	tags->tag = (unsigned char **) cli_realloc(tags->tag,
				tags->count * sizeof(char *));
	if (!tags->tag) {
		goto abort;
	}
	tags->value = (unsigned char **) cli_realloc(tags->value,
				tags->count * sizeof(char *));
	if (!tags->value) {
		goto abort;
	}
	tags->tag[tags->count-1] = strdup(tag);
	if (value) {
		if (*value == '"') {
			tags->value[tags->count-1] = strdup(value+1);
			len = strlen(value+1);
			if (len > 0) {
				tags->value[tags->count-1][len-1] = '\0';
			}
		} else {
			tags->value[tags->count-1] = strdup(value);
		}
	} else {
		tags->value[tags->count-1] = NULL;
	}
	return;
	
abort:
	/* Bad error - can't do 100% recovery */
	tags->count--;
	for (i=0; i < tags->count; i++) {
		if (tags->tag) {
			free(tags->tag[i]);
		}
		if (tags->value) {
			free(tags->value[i]);
		}
	}
	if (tags->tag) {
		free(tags->tag);
	}
	if (tags->value) {
		free(tags->value);
	}
	tags->tag = tags->value = NULL;
	tags->count = 0;	
	return;
}

static void html_output_tag(file_buff_t *fbuff, char *tag, tag_arguments_t *tags)
{
	int i, j, len;

	html_output_c(fbuff, NULL, '<');
	html_output_str(fbuff, tag, strlen(tag));
	for (i=0; i < tags->count; i++) {
		html_output_c(fbuff, NULL, ' ');
		html_output_str(fbuff, tags->tag[i], strlen(tags->tag[i]));
		if (tags->value[i]) {
			html_output_str(fbuff, "=\"", 2);
			len = strlen(tags->value[i]);
			for (j=0 ; j<len ; j++) {
				html_output_c(fbuff, NULL, tolower(tags->value[i][j]));
			}
			html_output_c(fbuff, NULL, '"');
		}
	}
	html_output_c(fbuff, NULL, '>');
}

void html_tag_arg_free(tag_arguments_t *tags)
{
	int i;
	
	for (i=0; i < tags->count; i++) {
		free(tags->tag[i]);
		if (tags->value[i]) {
			free(tags->value[i]);
		}
	}
	if (tags->tag) {
		free(tags->tag);
	}
	if (tags->value) {
		free(tags->value);
	}
	tags->tag = tags->value = NULL;
	tags->count = 0;
}

static int cli_html_normalise(int fd, m_area_t *m_area, const char *dirname, tag_arguments_t *hrefs)
{
	int fd_tmp, tag_length, tag_arg_length;
	int retval=FALSE, escape, value, hex, tag_val_length, table_pos, in_script=FALSE;
	FILE *stream_in;
	html_state state=HTML_NORM, next_state=HTML_BAD_STATE;
	char filename[1024], tag[HTML_STR_LENGTH+1], tag_arg[HTML_STR_LENGTH+1];
	char tag_val[HTML_STR_LENGTH+1];
	unsigned char *line, *ptr, *arg_value;
	tag_arguments_t tag_args;
	quoted_state quoted;
	unsigned long length;
	file_buff_t *file_buff_o1, *file_buff_o2, *file_buff_script;
	
	if (!m_area) {
		if (fd < 0) {
			cli_dbgmsg("Invalid HTML fd\n");
			return FALSE;
		}
		lseek(fd, 0, SEEK_SET);	
		fd_tmp = dup(fd);
		if (fd_tmp < 0) {
			return FALSE;
		}
		stream_in = fdopen(fd_tmp, "r");
		if (!stream_in) {
			close(fd_tmp);
			return FALSE;
		}
	}

	tag_args.count = 0;
	tag_args.tag = NULL;
	tag_args.value = NULL;
	
	if (dirname) {
		file_buff_o1 = (file_buff_t *) cli_malloc(sizeof(file_buff_t));
		if (!file_buff_o1) {
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}
		
		file_buff_o2 = (file_buff_t *) cli_malloc(sizeof(file_buff_t));
		if (!file_buff_o2) {
			free(file_buff_o1);
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}
		
		file_buff_script = (file_buff_t *) cli_malloc(sizeof(file_buff_t));
		if (!file_buff_script) {
			free(file_buff_o1);
			free(file_buff_o2);
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}
		
		snprintf(filename, 1024, "%s/comment.html", dirname);
		file_buff_o1->fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
		if (!file_buff_o1->fd) {
			cli_dbgmsg("open failed: %s\n", filename);
			free(file_buff_o1);
			free(file_buff_o2);
			free(file_buff_script);
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}

		snprintf(filename, 1024, "%s/nocomment.html", dirname);
		file_buff_o2->fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
		if (!file_buff_o2->fd) {
			cli_dbgmsg("open failed: %s\n", filename);
			close(file_buff_o1->fd);
			free(file_buff_o1);
			free(file_buff_o2);
			free(file_buff_script);
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}

		snprintf(filename, 1024, "%s/script.html", dirname);
		file_buff_script->fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
		if (!file_buff_script->fd) {
			cli_dbgmsg("open failed: %s\n", filename);
			close(file_buff_o1->fd);
			close(file_buff_o2->fd);
			free(file_buff_o1);
			free(file_buff_o2);
			free(file_buff_script);
			file_buff_o1 = file_buff_o2 = file_buff_script = NULL;
			goto abort;
		}

		file_buff_o1->length = 0;
		file_buff_o2->length = 0;
		file_buff_script->length = 0;
	} else {
		file_buff_o1 = NULL;
		file_buff_o2 = NULL;
		file_buff_script = NULL;
	}
			
	ptr = line = cli_readline(stream_in, m_area, 8192);
	while (line) {
		while (*ptr && isspace(*ptr)) {
			ptr++;
		}
		while (*ptr) {
			if (*ptr == '\n') {
				/* Convert it to a space and re-process */
				*ptr = ' ';
				continue;
			}
			if (*ptr == '\r') {
				ptr++;
				continue;
			}
			switch (state) {
			case HTML_BAD_STATE:
				/* An engine error has occurred */
				cli_dbgmsg("HTML Engine Error\n");
				goto abort;
			case HTML_SKIP_LENGTH:
				length--;
				ptr++;
				if (!length) {
					state = next_state;
				}
				break;
			case HTML_SKIP_WS:
				if (isspace(*ptr)) {
					ptr++;
				} else {
					state = next_state;
					next_state = HTML_BAD_STATE;
				}
				break;
			case HTML_TRIM_WS:
				if (isspace(*ptr)) {
					ptr++;
				} else {
					html_output_c(file_buff_o1, file_buff_o2, ' ');
					state = next_state;
					next_state = HTML_BAD_STATE;
				}
				break;
			case HTML_NORM:
				if (*ptr == '<') {
					html_output_c(file_buff_o1, file_buff_o2, '<');
					if (in_script) {
						html_output_c(file_buff_script, NULL, '<');
					}
					ptr++;
					state = HTML_SKIP_WS;
					tag_length=0;
					next_state = HTML_TAG;
				} else if (isspace(*ptr)) {
					state = HTML_TRIM_WS;
					next_state = HTML_NORM;
				} else if (*ptr == '&') {
					state = HTML_CHAR_REF;
					next_state = HTML_NORM;
					ptr++;
				} else {
					html_output_c(file_buff_o1, file_buff_o2, tolower(*ptr));
					if (in_script) {
						html_output_c(file_buff_script, NULL, tolower(*ptr));
					}
					ptr++;
				}
				break;
			case HTML_TAG:
				if ((tag_length == 0) && (*ptr == '!')) {
					/* Comment */
					html_output_c(file_buff_o1, NULL, '!');
					if (in_script) {
						html_output_c(file_buff_script, NULL, '!');
					}
					/* Need to rewind in the no-comment output stream */
					if (file_buff_o2 && (file_buff_o2->length > 0)) {
						file_buff_o2->length--;
					}
					state = HTML_COMMENT;
					next_state = HTML_BAD_STATE;
					ptr++;
				} else if (*ptr == '>') {
					html_output_c(file_buff_o1, file_buff_o2, '>');
					if (in_script) {
						html_output_c(file_buff_script, NULL, '>');
					}
					ptr++;
					tag[tag_length] = '\0';
					state = HTML_SKIP_WS;
					next_state = HTML_PROCESS_TAG;
				} else if (!isspace(*ptr)) {
					html_output_c(file_buff_o1, file_buff_o2, tolower(*ptr));
					if (in_script) {
						html_output_c(file_buff_script, NULL, tolower(*ptr));
					}
					if (tag_length < HTML_STR_LENGTH) {
						tag[tag_length++] = tolower(*ptr);
					}
					ptr++;
				}  else {
					tag[tag_length] = '\0';
					state = HTML_SKIP_WS;
					tag_arg_length = 0;
					next_state = HTML_TAG_ARG;
				}
				break;
			case HTML_TAG_ARG:
				if (*ptr == '=') {
					html_output_c(file_buff_o1, file_buff_o2, '=');
					tag_arg[tag_arg_length] = '\0';
					ptr++;
					state = HTML_SKIP_WS;
					escape = FALSE;
					quoted = NOT_QUOTED;
					tag_val_length = 0;
					next_state = HTML_TAG_ARG_VAL;
				} else if (isspace(*ptr)) {
					ptr++;
					tag_arg[tag_arg_length] = '\0';
					state = HTML_SKIP_WS;
					next_state = HTML_TAG_ARG_EQUAL;
				} else if (*ptr == '>') {
					html_output_c(file_buff_o1, file_buff_o2, '>');
					if (tag_arg_length > 0) {
						tag_arg[tag_arg_length] = '\0';
						html_tag_arg_add(&tag_args, tag_arg, NULL);
					}
					ptr++;
					state = HTML_PROCESS_TAG;
					next_state = HTML_BAD_STATE;
				} else {
					if (tag_arg_length == 0) {
						/* Start of new tag - add space */
						html_output_c(file_buff_o1, file_buff_o2,' ');
					}
					html_output_c(file_buff_o1, file_buff_o2, tolower(*ptr));
					if (tag_arg_length < HTML_STR_LENGTH) {
						tag_arg[tag_arg_length++] = tolower(*ptr);
					}
					ptr++;
				}
				break;
			case HTML_TAG_ARG_EQUAL:
				if (*ptr == '=') {
					html_output_c(file_buff_o1, file_buff_o2, '=');
					ptr++;
					state = HTML_SKIP_WS;
					escape = FALSE;
					quoted = NOT_QUOTED;
					tag_val_length = 0;
					next_state = HTML_TAG_ARG_VAL;
				} else {
					if (tag_arg_length > 0) {
						tag_arg[tag_arg_length] = '\0';
						html_tag_arg_add(&tag_args, tag_arg, NULL);
					}
					tag_arg_length=0;
					state = HTML_TAG_ARG;
					next_state = HTML_BAD_STATE;
				}
				break;
			case HTML_TAG_ARG_VAL:
				if (*ptr == '&') {
					state = HTML_CHAR_REF;
					next_state = HTML_TAG_ARG_VAL;
					ptr++;
				} else if (*ptr == '\'') {
					if (tag_val_length == 0) {
						quoted = SINGLE_QUOTED;
						html_output_c(file_buff_o1, file_buff_o2, '"');
						if (tag_val_length < HTML_STR_LENGTH) {
							tag_val[tag_val_length++] = '"';
						}
						ptr++;
					} else {
						if (!escape && (quoted==SINGLE_QUOTED)) {
							html_output_c(file_buff_o1, file_buff_o2, '"');
							if (tag_val_length < HTML_STR_LENGTH) {
								tag_val[tag_val_length++] = '"';
							}
							tag_val[tag_val_length] = '\0';
							html_tag_arg_add(&tag_args, tag_arg, tag_val);
							ptr++;
							state = HTML_SKIP_WS;
							tag_arg_length=0;
							next_state = HTML_TAG_ARG;
						} else {
							html_output_c(file_buff_o1, file_buff_o2, '"');
							if (tag_val_length < HTML_STR_LENGTH) {
								tag_val[tag_val_length++] = '"';
							}
							ptr++;
						}
					}
				} else if (*ptr == '"') {
					if (tag_val_length == 0) {
						quoted = DOUBLE_QUOTED;
						html_output_c(file_buff_o1, file_buff_o2, '"');
						if (tag_val_length < HTML_STR_LENGTH) {
							tag_val[tag_val_length++] = '"';
						}
						ptr++;
					} else {
						if (!escape && (quoted==DOUBLE_QUOTED)) {					
							html_output_c(file_buff_o1, file_buff_o2, '"');
							if (tag_val_length < HTML_STR_LENGTH) {
								tag_val[tag_val_length++] = '"';
							}
							tag_val[tag_val_length] = '\0';
							html_tag_arg_add(&tag_args, tag_arg, tag_val);
							ptr++;
							state = HTML_SKIP_WS;
							tag_arg_length=0;
							next_state = HTML_TAG_ARG;
						} else {
							html_output_c(file_buff_o1, file_buff_o2, '"');
							if (tag_val_length < HTML_STR_LENGTH) {
								tag_val[tag_val_length++] = '"';
							}
							ptr++;
						}
					}
				} else if (isspace(*ptr) || (*ptr == '>')) {
					if (quoted == NOT_QUOTED) {
						tag_val[tag_val_length] = '\0';
						html_tag_arg_add(&tag_args, tag_arg, tag_val);
						state = HTML_SKIP_WS;
						tag_arg_length=0;
						next_state = HTML_TAG_ARG;
					} else {
						html_output_c(file_buff_o1, file_buff_o2, *ptr);
						if (tag_val_length < HTML_STR_LENGTH) {
							if (isspace(*ptr)) {
								tag_val[tag_val_length++] = ' ';
							} else {
								tag_val[tag_val_length++] = '>';
							}
						}
						state = HTML_SKIP_WS;
						escape = FALSE;
						quoted = NOT_QUOTED;
						next_state = HTML_TAG_ARG_VAL;
						ptr++;
					}
				} else {
					html_output_c(file_buff_o1, file_buff_o2, tolower(*ptr));
					if (tag_val_length < HTML_STR_LENGTH) {
						tag_val[tag_val_length++] = *ptr;
					}
					ptr++;
				}
				
				if (*ptr == '\\') {
					escape = TRUE;
				} else {
					escape = FALSE;
				}
				break;
			case HTML_COMMENT:
				html_output_c(file_buff_o1, NULL, tolower(*ptr));
				if (in_script) {
					html_output_c(file_buff_script, NULL, tolower(*ptr));
				}
				if (*ptr == '>') {
					state = HTML_SKIP_WS;
					next_state = HTML_NORM;	
				}
				ptr++;
				break;
			case HTML_PROCESS_TAG:
				
				/* Default to no action for this tag */
				state = HTML_SKIP_WS;
				next_state = HTML_NORM;
				if (tag[0] == '/') {
					/* End tag */
					state = HTML_SKIP_WS;
					next_state = HTML_NORM;
					if (strcmp(tag, "/script") == 0) {
						in_script=FALSE;
						html_output_c(file_buff_script, NULL, '\n');
					}
				} else if (strcmp(tag, "script") == 0) {
					arg_value = html_tag_arg_value(&tag_args, "language");
					if (arg_value && (strcasecmp(arg_value, "jscript.encode") == 0)) {
						html_tag_arg_set(&tag_args, "language", "javascript");
						state = HTML_SKIP_WS;
						next_state = HTML_JSDECODE;
					} else if (arg_value && (strcasecmp(arg_value, "vbscript.encode") == 0)) {
						html_tag_arg_set(&tag_args, "language", "vbscript");
						state = HTML_SKIP_WS;
						next_state = HTML_JSDECODE;
					} else {
						in_script = TRUE;
					}
					html_output_tag(file_buff_script, tag, &tag_args);
				} else if (hrefs && strcmp(tag, "a") == 0) {
					arg_value = html_tag_arg_value(&tag_args, "href");
					if (arg_value && strlen(arg_value) > 0) {
						html_tag_arg_add(hrefs, "href", arg_value);
					}
				}
				html_tag_arg_free(&tag_args);
				break;
			case HTML_CHAR_REF:
				if (*ptr == '#') {
					value = 0;
					hex = FALSE;
					state = HTML_CHAR_REF_DECODE;
					ptr++;
				} else {
					html_output_c(file_buff_o1, file_buff_o2, '&');
					state = next_state;
					next_state = HTML_BAD_STATE;
				}
				break;
			case HTML_CHAR_REF_DECODE:
				if ((value==0) && ((*ptr == 'x') || (*ptr == 'X'))) {
					hex=TRUE;
					ptr++;
				} else if (*ptr == ';') {
					html_output_c(file_buff_o1, file_buff_o2, value);
					state = next_state;
					next_state = HTML_BAD_STATE;
					ptr++;
				} else if (isdigit(*ptr) || (hex && isxdigit(*ptr))) {
					if (hex) {
						value *= 16;
					} else {
						value *= 10;
					}
					if (isdigit(*ptr)) {
						value += (*ptr - '0');
					} else {
						value += (tolower(*ptr) - 'a' + 10);
					}
					ptr++;
				} else {
					html_output_c(file_buff_o1, file_buff_o2, value);
					state = next_state;
					next_state = HTML_BAD_STATE;
				}
				break;
			case HTML_JSDECODE:
				/* Check for start marker */
				if (strncmp(ptr, "#@~^", 4) == 0) {
					ptr += 4;
					state = HTML_JSDECODE_LENGTH;
					next_state = HTML_BAD_STATE;
				} else {
					html_output_c(file_buff_o1, file_buff_o2, tolower(*ptr));
					html_output_c(file_buff_script, NULL, tolower(*ptr));
					ptr++;
				}
				break;
			case HTML_JSDECODE_LENGTH:
				if (strlen(ptr) < 8) {
					state = HTML_NORM;
					next_state = HTML_BAD_STATE;
					break;
				}
				length = base64_chars[ptr[0]] << 2;
				length += base64_chars[ptr[1]] >> 4;
				length += (base64_chars[ptr[1]] & 0x0f) << 12;
				length += (base64_chars[ptr[2]] >> 2) << 8;
				length += (base64_chars[ptr[2]] & 0x03) << 22;
				length += base64_chars[ptr[3]] << 16;
				length += (base64_chars[ptr[4]] << 2) << 24;
				length += (base64_chars[ptr[5]] >> 4) << 24;
				table_pos = 0;
				state = HTML_JSDECODE_DECRYPT;
				next_state = HTML_BAD_STATE;
				ptr += 8;
				break;
			case HTML_JSDECODE_DECRYPT:
				if (length == 0) {
					html_output_str(file_buff_script, "</script>\n", 10);
					length = 12;
					state = HTML_SKIP_LENGTH;
					next_state = HTML_NORM;
					break;
				}
				if (*ptr < 0x80) {
					value = decrypt_tables[table_order[table_pos]][*ptr];
					if (value == 0xFF) { /* special character */
						ptr++;
						length--;
						switch (*ptr) {
						case '\0':
							/* Fixup for end of line */
							ptr--;
							break;
						case 0x21:
							html_output_c(file_buff_o1, file_buff_o2, 0x3c);
							html_output_c(file_buff_script, NULL, 0x3c);
							break;
						case 0x23:
							html_output_c(file_buff_o1, file_buff_o2, 0x0d);
							html_output_c(file_buff_script, NULL, 0x0d);
							break;
						case 0x24:
							html_output_c(file_buff_o1, file_buff_o2, 0x40);
							html_output_c(file_buff_script, NULL, 0x40);
							break;				
						case 0x26:
							html_output_c(file_buff_o1, file_buff_o2, 0x0a);
							html_output_c(file_buff_script, NULL, 0x0a);
							break;
						case 0x2a:
							html_output_c(file_buff_o1, file_buff_o2, 0x3e);
							html_output_c(file_buff_script, NULL, 0x3e);
							break;
						}
					} else {
						html_output_c(file_buff_o1, file_buff_o2, value);
						html_output_c(file_buff_script, NULL, tolower(value));
					}
				}
				table_pos = (table_pos + 1) % 64;
				ptr++;
				length--;
				break;
			}
		}
		free(line);
		ptr = line = cli_readline(stream_in, m_area, 8192);
	}
	
	retval = TRUE;
abort:
	html_tag_arg_free(&tag_args);
	if (!m_area) {
		fclose(stream_in);
	}
	if (file_buff_o1) {
		html_output_flush(file_buff_o1);
		close(file_buff_o1->fd);
		free(file_buff_o1);
	}
	if (file_buff_o2) {
		html_output_flush(file_buff_o2);
		close(file_buff_o2->fd);
		free(file_buff_o2);
	}
	if (file_buff_script) {
		html_output_flush(file_buff_script);
		close(file_buff_script->fd);
		free(file_buff_script);
	}
	return retval;
}

int html_normalise_mem(unsigned char *in_buff, off_t in_size, const char *dirname, tag_arguments_t *hrefs)
{
	m_area_t m_area;
	
	m_area.buffer = in_buff;
	m_area.length = in_size;
	m_area.offset = 0;
	
	return cli_html_normalise(-1, &m_area, dirname, hrefs);
}

int html_normalise_fd(int fd, const char *dirname, tag_arguments_t *hrefs)
{
#if HAVE_MMAP
	int retval=FALSE;
	m_area_t m_area;
	struct stat statbuf;
	
	if (fstat(fd, &statbuf) == 0) {
		m_area.length = statbuf.st_size;
		m_area.buffer = (unsigned char *) mmap(NULL, m_area.length, PROT_READ, MAP_PRIVATE, fd, 0);
		m_area.offset = 0;
		if (m_area.buffer == MAP_FAILED) {
			cli_dbgmsg("mmap HTML failed\n");
			retval = cli_html_normalise(fd, NULL, dirname, hrefs);
		} else {
			cli_dbgmsg("mmap'ed file\n");
			retval = cli_html_normalise(-1, &m_area, dirname, hrefs);
			munmap(m_area.buffer, m_area.length);
		}
	} else {
		cli_dbgmsg("fstat HTML failed\n");
		retval = cli_html_normalise(fd, NULL, dirname, hrefs);
	}
	return retval;
#else
	return cli_html_normalise(fd, NULL, dirname, hrefs);
#endif
}

int html_screnc_decode(int fd, const char *dirname)
{
	int fd_tmp, table_pos=0, result, count, state, retval=FALSE;
	unsigned char *line, tmpstr[6];
	unsigned long length;
	unsigned char *ptr, filename[1024];
	FILE *stream_in;
	file_buff_t file_buff;
	
	lseek(fd, 0, SEEK_SET);	
	fd_tmp = dup(fd);
	if (fd_tmp < 0) {
		return FALSE;
	}
	stream_in = fdopen(fd_tmp, "r");
	if (!stream_in) {
		close(fd_tmp);
		return FALSE;
	}
	
	snprintf(filename, 1024, "%s/screnc.html", dirname);
	file_buff.fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	file_buff.length = 0;
	
	if (!file_buff.fd) {
		cli_dbgmsg("open failed: %s\n", filename);
		fclose(stream_in);
		return FALSE;
	}
	
	while ((line = cli_readline(stream_in, NULL, 8192)) != NULL) {
		ptr = strstr(line, "#@~^");
		if (ptr) {
			break;
		}
		free(line);
        }
	if (!line) {
		goto abort;
	}
	
	/* Calculate the length of the encoded string */
	ptr += 4;
	count = 0;
	do {
		if (! *ptr) {
			free(line);
			ptr = line = cli_readline(stream_in, NULL, 8192);
			if (!line) {
				goto abort;
			}
		}
		tmpstr[count++] = *ptr;
		ptr++;
	} while (count < 6);
	
	length = base64_chars[tmpstr[0]] << 2;
	length += base64_chars[tmpstr[1]] >> 4;
	length += (base64_chars[tmpstr[1]] & 0x0f) << 12;
	length += (base64_chars[tmpstr[2]] >> 2) << 8;
	length += (base64_chars[tmpstr[2]] & 0x03) << 22;
	length += base64_chars[tmpstr[3]] << 16;
	length += (base64_chars[tmpstr[4]] << 2) << 24;
	length += (base64_chars[tmpstr[5]] >> 4) << 24;

	/* Move forward 2 bytes */
	count = 2;
	state = HTML_SKIP_LENGTH;

	while (length && line) {
		while (length && *ptr) {
			if ((*ptr == '\n') || (*ptr == '\r')) {
				ptr++;
				continue;
			}
			switch (state) {
			case HTML_SKIP_LENGTH:
				ptr++;
				count--;
				if (count == 0) {
					state = HTML_NORM;
				}
				break;
			case HTML_SPECIAL_CHAR:
				switch (*ptr) {
				case 0x21:
					html_output_c(&file_buff, NULL, 0x3c);
					break;
				case 0x23:
					html_output_c(&file_buff, NULL, 0x0d);
					break;
				case 0x24:
					html_output_c(&file_buff, NULL, 0x40);
					break;				
				case 0x26:
					html_output_c(&file_buff, NULL, 0x0a);
					break;
				case 0x2a:
					html_output_c(&file_buff, NULL, 0x3e);
					break;
				}
				ptr++;
				length--;
				state = HTML_NORM;
				break;
			case HTML_NORM:	
				if (*ptr < 0x80) {
					result = decrypt_tables[table_order[table_pos]][*ptr];
					if (result == 0xFF) { /* special character */
						state = HTML_SPECIAL_CHAR;
					} else {
						html_output_c(&file_buff, NULL, (char)result);
					}
				}
				ptr++;
				length--;
				table_pos = (table_pos + 1) % 64;
				break;
			}
		}
		free(line);
		if (length) {
			ptr = line = cli_readline(stream_in, NULL, 8192);
		}
	}
	retval = TRUE;
						
abort:
	fclose(stream_in);
	html_output_flush(&file_buff);
	close(file_buff.fd);
	return retval;
}