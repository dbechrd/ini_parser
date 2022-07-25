#define _CRT_SECURE_NO_WARNINGS

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct buffer {
    char *data;
    int length;
};

// slice is essentialy same thing, but doesn't own the memory, just points into a buffer
typedef buffer slice;

struct ini_kv {
    slice section;
    slice key;
    slice value;
};

struct ini_parser {
    buffer buf;                     // file buffer
    int cursor;                     // current offset in buffer where parser is
    int line;                       // current line # being parsed (for nice errors)
    slice section;                  // current [section] we're in (if any)
    std::vector<ini_kv> properties; // key-value properties read in so far
};

void init_ini(ini_parser *ini, buffer buf);
int parse_ini(ini_parser *ini);
void discard_whitespace(ini_parser *ini);
void discard_comment(ini_parser *ini);
int parse_section_header(ini_parser *ini);
int parse_kv(ini_parser *ini);

void init_ini(ini_parser *ini, buffer buf) {
    ini->buf = buf;
    ini->line = 1;
}

int parse_ini(ini_parser *ini)
{
    while (ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case '\r': {
                if (ini->cursor < ini->buf.length - 1 && ini->buf.data[ini->cursor + 1] == '\n') {
                    // handle Windows newlines (\r\n) by just ignoring the \r and let the \n later
                    // increment the line count
                } else {
                    // keep track of line # for pretty error messages
                    ini->line++;
                }
                break;
            }
            case '\n': {
                // keep track of line # for pretty error messages
                ini->line++;
                break;
            }
            case ' ': case '\t': {
                // ignore whitespace characters
                break;
            }
            case ';': {
                // discard comments
                discard_comment(ini);
                break;
            }
            case '[': {
                // read section header, check for errors
                int err = parse_section_header(ini);
                if (err < 0) {
                    return err;
                }
                break;
            }
            default: {
                // parse key=value line
                int err = parse_kv(ini);
                if (err < 0) {
                    return err;
                }
                break;
            }
        }
        ini->cursor++;
    }
    return 0;
}

void discard_whitespace(ini_parser *ini)
{
    while (ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case ' ': case '\t': {
                // skip whitespace
                break;
            }
            default: {
                // we found something interesting, return so it can be parsed by someone
                return;
            }
        }
        ini->cursor++;
    }
}

void discard_comment(ini_parser *ini)
{
    // Discard everything until the next newline
    while (ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case '\r': case '\n': {
                // end of line = end of comment, all done
                return;
            }
        }
        ini->cursor++;
    }
}

int parse_section_header(ini_parser *ini)
{
    // Discard '['
    ini->cursor++;

    char *section_begin = &ini->buf.data[ini->cursor];

    bool section_done = false;
    while (!section_done && ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case ']': {
                // End of section header, update current section info in the reader
                char *section_end = &ini->buf.data[ini->cursor];
                ini->section.data = section_begin;
                ini->section.length = section_end - section_begin;

                section_done = true;
                return 0;
            }
        }
        ini->cursor++;
    }

    if (!section_done) {
        fprintf(stderr, "[line %d] Expected ']', encountered EOF instead\n", ini->line);
        return -1;
    }
}


int parse_kv(ini_parser *ini)
{
    // Allowed whitespace:
    //  key  =  value
    // ^1  ^2  ^3    ^4
    // 1: leading whitespace
    // 2: whitespace before '='
    // 3: whitespace after '='
    // 4: trailing whitespace (handled in parse_ini after this function returns)

    // discard any whitespace before key
    discard_whitespace(ini);

    char *key_begin = &ini->buf.data[ini->cursor];
    char *key_end = 0;

    // Read key
    bool key_done = false;
    while (!key_done && ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case '=': {
                // End of key section, update key info
                key_done = true;
                break;
            }
            case ' ': case '\t': {
                // Ignore whitespace after key, before '='
                break;
            }
            case '\r': case '\n': {
                // Wtf? Why is there a newline before the equal sign??
                fprintf(stderr, "[line %d] Expected '=', encountered EOL instead\n", ini->line);
                return -1;
            }
            default: {
                // Any other character extends the current key, keep track of where key ends so far
                key_end = &ini->buf.data[ini->cursor + 1];
                break;
            }
        }
        ini->cursor++;
    }

    if (!key_done) {
        fprintf(stderr, "[line %d] Expected '=' after key, encountered EOF instead\n", ini->line);
        return -1;
    }

    // discard any whitespace before value
    discard_whitespace(ini);

    char *value_begin = &ini->buf.data[ini->cursor];
    char *value_end = 0;

    // Read value
    bool value_done = false;
    while (!value_done && ini->cursor < ini->buf.length) {
        switch (ini->buf.data[ini->cursor]) {
            case '\r': case '\n': {
                // End of value section, update value info
                value_done = true;
                break;
            }
            case ' ': case '\t': {
                // Ignore whitespace after value, before newline
                break;
            }
            default: {
                // Any other character extends the current value, keep track of where value ends so far
                value_end = &ini->buf.data[ini->cursor + 1];
                break;
            }
        }
        ini->cursor++;
    }

    if (!value_end) {
        fprintf(stderr, "[line %d] Expected value after '=', encountered EOF isntead\n", ini->line);
        return -1;
    }

    // Sanity check my work :D
    assert(key_begin);
    assert(key_end);
    assert(value_begin);
    assert(value_end);

    ini_kv kv{};
    kv.section = ini->section;
    kv.key.data = key_begin;
    kv.key.length = key_end - key_begin;
    kv.value.data = value_begin;
    kv.value.length = value_end - value_begin;
    ini->properties.push_back(kv);
    return 0;
}

// Read file as binary, but append \0 to the end of the buffer
// filename: name of file to read
// buf     : if file read successfully, this will be an allocated buffer containing file contents
// Returns 0 on success, negative error code on failure (see stderr for more info)
// WARN: you must free(buf->data) when you're done with it!!
int read_entire_file(const char *filename, buffer *buf)
{
    assert(filename);
    assert(buf);

    // Open file
    FILE *fs = fopen(filename, "rb");
    if (!fs) {
        fprintf(stderr, "Unable to open %s for reading\n", filename);
        return -1;
    }

    // Calculate length
    fseek(fs, 0, SEEK_END);
    long tell = ftell(fs);
    if (tell < 0) {
        fprintf(stderr, "Unable to determine length of %s\n", filename);
        return -2;
    }
    rewind(fs);

    // Error on empty file
    assert(tell > 0);

    // Allocate buffer
    size_t buflen = (size_t)tell;
    buf->data = (char *)calloc(1, buflen + 1);  // extra byte for nil
    if (!buf->data) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return -3;
    }

    // Read file into buffer
    size_t read = fread(buf->data, 1, tell, fs);
    if (read != buflen) {
        free(buf->data);
        fprintf(stderr, "Failed to read entire file\n");
        return -4;
    }

    // Close file
    fclose(fs);

    buf->length = (int)buflen;
    return 0;
}

int main()
{
    int err = 0;

    // Read the file
    buffer buf{};
    err = read_entire_file("test.ini", &buf);
    if (err < 0) {
        fprintf(stderr, "Failed to read .ini file :(\n");
        return err;
    }

    // Parse the file
    ini_parser ini{};
    init_ini(&ini, buf);
    err = parse_ini(&ini);
    if (err < 0) {
        fprintf(stderr, "Failed to parse .ini file :(\n");
        return err;
    }

    // Print properties to console to show how to use the slices
    for (auto &kv : ini.properties) {
        printf("[%.*s] %.*s = %.*s\n",
            kv.section.length, kv.section.data,
            kv.key.length, kv.key.data,
            kv.value.length, kv.value.data
        );
    }

    free(buf.data);

    getchar();
    return 0;
}