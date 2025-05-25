// Clean shared memory segments which have no processes attached to them.
// $ awk '{if ($7 == 0) print $2}' /proc/sysvipc/shm | xargs -r -L 1 ipcrm -m

#include <stdio.h>      // FILE, fopen, fread, feof
#include <stdlib.h>     // malloc, realloc
#include <stddef.h>     // size_t, NULL
#include <ctype.h>      // isspace
#include <stdbool.h>    // true
#include <string.h>     // strncmp

#include <sys/shm.h>    // shmat, shmdt, shmctl

#define lengthof(literal) (sizeof(literal) - 1)

size_t skip_spaces(char *buffer, char *buffer_end) {
    char *buffer_iter = buffer;

    while (buffer_iter != buffer_end && isspace(*buffer_iter)) {
        buffer_iter += 1;
    }

    return (size_t)(buffer_iter - buffer);
}

size_t read_line(char *buffer, char *buffer_end, char **line, char **line_end) {
    char *buffer_iter = buffer;

    *line = buffer_iter;
    while (buffer_iter != buffer_end && *buffer_iter != '\n') {
        buffer_iter += 1;
    }
    *line_end = buffer_iter;

    // Skip over the newline.
    buffer_iter += 1;
    return (size_t)(buffer_iter - buffer);
}

size_t read_word(char *buffer, char *buffer_end, char **word, char **word_end) {
    char *buffer_iter = buffer;

    while (buffer_iter != buffer_end && isspace(*buffer_iter)) {
        buffer_iter += 1;
    }

    *word = buffer_iter;
    while (buffer_iter != buffer_end && !isspace(*buffer_iter)) {
        buffer_iter += 1;
    }
    *word_end = buffer_iter;

    return (size_t)(buffer_iter - buffer);
}

int string_to_int(char *string, char *string_end) {
    int value = 0;

    char *string_iter = string;
    while (string_iter != string_end) {
        value = value * 10 + (*string_iter - '0');
        string_iter += 1;
    }

    return value;
}

int main(void) {
    // Find out the name of the file:
    // $ strace ipcs -m 2>&1 1>/dev/null | grep open
    FILE *file = fopen("/proc/sysvipc/shm", "rb");
    if (file == NULL) {
        return 1;
    }

    size_t buffer_capacity = 4096;
    size_t buffer_size = 0;
    char *buffer = malloc(buffer_capacity);
    if (buffer == NULL) {
        return 1;
    }

    // There is no other way to get this file into memory: neither fseek+ftell nor mmap work here.
    while (feof(file) == 0) {
        if (buffer_size == buffer_capacity) {
            buffer_capacity = buffer_capacity * 3 / 2;
            buffer = realloc(buffer, buffer_capacity);
            if (buffer == NULL) {
                return 1;
            }
        }

        size_t bytes_read = fread(buffer + buffer_size, 1, buffer_capacity - buffer_size, file);
        if (bytes_read != buffer_capacity - buffer_size && ferror(file) != 0) {
            return 1;
        }

        buffer_size += bytes_read;
    }

    char *buffer_iter = buffer;
    char *buffer_end = buffer + buffer_size;
    buffer_iter += skip_spaces(buffer_iter, buffer_end);

    int shmid_column = -1;
    int nattch_column = -1;
    {
        char *header;
        char *header_end;
        buffer_iter += read_line(buffer_iter, buffer_end, &header, &header_end);
        if (header == header_end) {
            return 1;
        }

        int column_index = 0;
        char *header_iter = header;
        while (true) {
            char *column;
            char *column_end;
            header_iter += read_word(header_iter, header_end, &column, &column_end);
            if (column == column_end) {
                break;
            }

            if (
                lengthof("shmid") == (size_t)(column_end - column) &&
                strncmp("shmid", column, (size_t)(column_end - column)) == 0
            ) {
                shmid_column = column_index;
            }

            if (
                lengthof("nattch") == (size_t)(column_end - column) &&
                strncmp("nattch", column, (size_t)(column_end - column)) == 0
            ) {
                nattch_column = column_index;
            }

            column_index += 1;
        }
    }
    if (shmid_column == -1 || nattch_column == -1) {
        return 1;
    }

    while (true) {
        char *line;
        char *line_end;
        buffer_iter += read_line(buffer_iter, buffer_end, &line, &line_end);
        if (line == line_end) {
            break;
        }

        int shmid = -1;
        int nattch = -1;

        int column_index = 0;
        char *line_iter = line;
        while (true) {
            char *value;
            char *value_end;
            line_iter += read_word(line_iter, line_end, &value, &value_end);
            if (value == value_end) {
                break;
            }

            if (shmid_column == column_index) {
                shmid = string_to_int(value, value_end);
            }

            if (nattch_column == column_index) {
                nattch = string_to_int(value, value_end);
            }

            column_index += 1;
        }

        if (shmid != -1 && nattch != -1) {
            if (nattch == 0) {
                void *address = shmat(shmid, 0, 0);
                shmdt(address);
                shmctl(shmid, IPC_RMID, 0);
            }
        }
    }

    return 0;
}
