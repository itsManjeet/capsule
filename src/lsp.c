#define _XOPEN_SOURCE 700
#include "lipi.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    char *uri;
    char *path;
    char *text;
} LspDocument;

typedef struct {
    LipiCompiler *base;
    LipiVec documents; /* LspDocument* */
    int shutdown;
    int exiting;
} LspServer;

static char *lsp_strdup(const char *s) {
    char *out = strdup(s ? s : "");
    if (!out) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    return out;
}

static char *lsp_strndup(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

static void lsp_json_escape(LipiStr *out, const char *text) {
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
        switch (*p) {
        case '"': lipi_str_append(out, "\\\""); break;
        case '\\': lipi_str_append(out, "\\\\"); break;
        case '\b': lipi_str_append(out, "\\b"); break;
        case '\f': lipi_str_append(out, "\\f"); break;
        case '\n': lipi_str_append(out, "\\n"); break;
        case '\r': lipi_str_append(out, "\\r"); break;
        case '\t': lipi_str_append(out, "\\t"); break;
        default:
            if (*p < 0x20) {
                lipi_str_appendf(out, "\\u%04x", (unsigned int)*p);
            } else {
                lipi_str_append_n(out, (const char *)p, 1);
            }
            break;
        }
    }
}

static void lsp_send_json(const char *json) {
    size_t len = strlen(json);
    printf("Content-Length: %zu\r\n\r\n", len);
    fwrite(json, 1, len, stdout);
    fflush(stdout);
}

static void lsp_send_response(const char *id, const char *result_json) {
    LipiStr out = {0};
    lipi_str_append(&out, "{\"jsonrpc\":\"2.0\",\"id\":");
    lipi_str_append(&out, id ? id : "null");
    lipi_str_append(&out, ",\"result\":");
    lipi_str_append(&out, result_json ? result_json : "null");
    lipi_str_append(&out, "}");
    lsp_send_json(out.data ? out.data : "{}");
    lipi_str_free(&out);
}

static void lsp_send_error(const char *id, int code, const char *message) {
    LipiStr out = {0};
    lipi_str_append(&out, "{\"jsonrpc\":\"2.0\",\"id\":");
    lipi_str_append(&out, id ? id : "null");
    lipi_str_appendf(&out, ",\"error\":{\"code\":%d,\"message\":\"", code);
    lsp_json_escape(&out, message ? message : "error");
    lipi_str_append(&out, "\"}}");
    lsp_send_json(out.data ? out.data : "{}");
    lipi_str_free(&out);
}

static const char *json_skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *json_skip_string(const char *p) {
    if (*p != '"') {
        return p;
    }
    p++;
    while (*p) {
        if (*p == '\\') {
            if (p[1]) {
                p += 2;
            } else {
                p++;
            }
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        p++;
    }
    return p;
}

static int json_key_equals(const char *start, const char *key) {
    const char *p = start + 1;
    const char *k = key;
    while (*p && *p != '"' && *k) {
        if (*p == '\\') {
            return 0;
        }
        if (*p != *k) {
            return 0;
        }
        p++;
        k++;
    }
    return *k == 0 && *p == '"';
}

static const char *json_find_key(const char *json, const char *key) {
    const char *p = json;
    while (*p) {
        if (*p != '"') {
            p++;
            continue;
        }
        const char *start = p;
        const char *end = json_skip_string(p);
        const char *after = json_skip_ws(end);
        if (*after == ':' && json_key_equals(start, key)) {
            return json_skip_ws(after + 1);
        }
        p = end;
    }
    return NULL;
}

static char hex_digit(int ch) {
    if (ch >= '0' && ch <= '9') return (char)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (char)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return (char)(ch - 'A' + 10);
    return -1;
}

static char *json_parse_string_value(const char *p) {
    if (!p || *p != '"') {
        return NULL;
    }
    p++;
    LipiStr out = {0};
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"': lipi_str_append_n(&out, "\"", 1); p++; break;
            case '\\': lipi_str_append_n(&out, "\\", 1); p++; break;
            case '/': lipi_str_append_n(&out, "/", 1); p++; break;
            case 'b': lipi_str_append_n(&out, "\b", 1); p++; break;
            case 'f': lipi_str_append_n(&out, "\f", 1); p++; break;
            case 'n': lipi_str_append_n(&out, "\n", 1); p++; break;
            case 'r': lipi_str_append_n(&out, "\r", 1); p++; break;
            case 't': lipi_str_append_n(&out, "\t", 1); p++; break;
            case 'u':
                /* LIPI source is currently byte-oriented. Preserve valid ASCII Unicode escapes. */
                if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) &&
                    isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                    int value = (hex_digit(p[1]) << 12) | (hex_digit(p[2]) << 8) |
                                (hex_digit(p[3]) << 4) | hex_digit(p[4]);
                    char ch = (value >= 0 && value < 128) ? (char)value : '?';
                    lipi_str_append_n(&out, &ch, 1);
                    p += 5;
                } else if (*p) {
                    p++;
                }
                break;
            default:
                if (*p) {
                    lipi_str_append_n(&out, p, 1);
                    p++;
                }
                break;
            }
            continue;
        }
        lipi_str_append_n(&out, p, 1);
        p++;
    }
    char *res = lsp_strdup(out.data ? out.data : "");
    lipi_str_free(&out);
    return res;
}

static char *json_get_string(const char *json, const char *key) {
    return json_parse_string_value(json_find_key(json, key));
}

static const char *json_skip_value(const char *p) {
    p = json_skip_ws(p);
    if (*p == '"') {
        return json_skip_string(p);
    }
    if (*p == '{') {
        int depth = 0;
        do {
            if (*p == '"') {
                p = json_skip_string(p);
                continue;
            }
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            p++;
        } while (*p && depth > 0);
        return p;
    }
    if (*p == '[') {
        int depth = 0;
        do {
            if (*p == '"') {
                p = json_skip_string(p);
                continue;
            }
            if (*p == '[') depth++;
            if (*p == ']') depth--;
            p++;
        } while (*p && depth > 0);
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

static char *json_get_id_raw(const char *json) {
    const char *value = json_find_key(json, "id");
    if (!value) {
        return NULL;
    }
    const char *end = json_skip_value(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    return lsp_strndup(value, (size_t)(end - value));
}

static int lsp_read_message(char **out_body) {
    char line[4096];
    long content_length = -1;
    while (fgets(line, sizeof(line), stdin)) {
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            break;
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = strtol(line + 15, NULL, 10);
        }
    }
    if (content_length < 0) {
        return 0;
    }
    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    size_t got = fread(body, 1, (size_t)content_length, stdin);
    if (got != (size_t)content_length) {
        free(body);
        return 0;
    }
    body[got] = 0;
    *out_body = body;
    return 1;
}

static char *lsp_percent_decode(const char *text) {
    LipiStr out = {0};
    for (const char *p = text; *p; p++) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hi = hex_digit(p[1]);
            char lo = hex_digit(p[2]);
            char ch = (char)((hi << 4) | lo);
            lipi_str_append_n(&out, &ch, 1);
            p += 2;
        } else {
            lipi_str_append_n(&out, p, 1);
        }
    }
    char *res = lsp_strdup(out.data ? out.data : "");
    lipi_str_free(&out);
    return res;
}

static char *lsp_uri_to_path(const char *uri) {
    if (!uri) {
        return lsp_strdup("<unknown>");
    }
    if (strncmp(uri, "file://", 7) != 0) {
        return lsp_strdup(uri);
    }
    const char *path = uri + 7;
    if (path[0] != '/') {
        const char *slash = strchr(path, '/');
        path = slash ? slash : path;
    }
    return lsp_percent_decode(path);
}

static char *lsp_canonical_path(const char *path) {
    char buf[PATH_MAX];
    if (realpath(path, buf)) {
        return lsp_strdup(buf);
    }
    return lsp_strdup(path);
}

static LspDocument *lsp_find_document(LspServer *server, const char *uri) {
    for (int i = 0; i < server->documents.len; i++) {
        LspDocument *doc = (LspDocument *)server->documents.items[i];
        if (strcmp(doc->uri, uri) == 0) {
            return doc;
        }
    }
    return NULL;
}

static LspDocument *lsp_upsert_document(LspServer *server, const char *uri, const char *text) {
    LspDocument *doc = lsp_find_document(server, uri);
    if (!doc) {
        doc = (LspDocument *)calloc(1, sizeof(LspDocument));
        if (!doc) {
            fprintf(stderr, "lipi: out of memory\n");
            exit(2);
        }
        doc->uri = lsp_strdup(uri);
        char *path = lsp_uri_to_path(uri);
        doc->path = lsp_canonical_path(path);
        free(path);
        lipi_vec_push(&server->documents, doc);
    }
    free(doc->text);
    doc->text = lsp_strdup(text ? text : "");
    return doc;
}

static void lsp_close_document(LspServer *server, const char *uri) {
    for (int i = 0; i < server->documents.len; i++) {
        LspDocument *doc = (LspDocument *)server->documents.items[i];
        if (strcmp(doc->uri, uri) != 0) {
            continue;
        }
        free(doc->uri);
        free(doc->path);
        free(doc->text);
        free(doc);
        memmove(&server->documents.items[i], &server->documents.items[i + 1],
                (size_t)(server->documents.len - i - 1) * sizeof(void *));
        server->documents.len--;
        return;
    }
}

static void lsp_compiler_init(LspServer *server, LipiCompiler *c) {
    memset(c, 0, sizeof(*c));
    c->arch = server->base->arch ? server->base->arch : "x86_64";
    c->platform = server->base->platform ? server->base->platform : "linux";
    c->stdlib_path = server->base->stdlib_path;
    c->quiet = 1;
    LipiStr target = {0};
    lipi_str_appendf(&target, "%s-%s", c->arch, c->platform);
    c->target = lipi_arena_strdup(&c->arena, target.data ? target.data : "x86_64-linux");
    lipi_str_free(&target);
}

static void lsp_compiler_cleanup(LipiCompiler *c) {
    for (int i = 0; i < c->sources.len; i++) {
        LipiSource *src = (LipiSource *)c->sources.items[i];
        free(src->text);
        free(src->line_offsets);
    }
    free(c->sources.items);
    free(c->forms.items);
    free(c->symbols.items);
    free(c->globals.items);
    free(c->functions.items);
    free(c->macros.items);
    free(c->inline_c.items);
    free(c->loaded_files.items);
    free(c->diagnostics.items);
    lipi_arena_free(&c->arena);
}

static int lsp_same_file(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static void lsp_publish_diagnostics(LspDocument *doc, LipiCompiler *c) {
    LipiStr out = {0};
    lipi_str_append(&out, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"");
    lsp_json_escape(&out, doc->uri);
    lipi_str_append(&out, "\",\"diagnostics\":[");
    int emitted = 0;
    for (int i = 0; i < c->diagnostics.len; i++) {
        LipiDiag *diag = (LipiDiag *)c->diagnostics.items[i];
        if (!lsp_same_file(diag->primary.file, doc->path)) {
            continue;
        }
        if (emitted) {
            lipi_str_append(&out, ",");
        }
        int line = diag->primary.line > 0 ? diag->primary.line - 1 : 0;
        int col = diag->primary.column > 0 ? diag->primary.column - 1 : 0;
        int len = diag->primary.length > 0 ? diag->primary.length : 1;
        lipi_str_appendf(&out,
                         "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                         "\"end\":{\"line\":%d,\"character\":%d}},\"severity\":1,\"source\":\"lipi\",\"message\":\"",
                         line, col, line, col + len);
        lsp_json_escape(&out, diag->message);
        if (diag->detail[0]) {
            lipi_str_append(&out, "\\n");
            lsp_json_escape(&out, diag->detail);
        }
        if (diag->has_secondary) {
            lipi_str_append(&out, "\\nprevious definition here: ");
            lsp_json_escape(&out, diag->secondary.file ? diag->secondary.file : "<unknown>");
            lipi_str_appendf(&out, ":%d", diag->secondary.line);
        }
        lipi_str_append(&out, "\"}");
        emitted++;
    }
    lipi_str_append(&out, "]}}");
    lsp_send_json(out.data ? out.data : "{}");
    lipi_str_free(&out);
}

static void lsp_publish_empty_diagnostics(const char *uri) {
    LipiStr out = {0};
    lipi_str_append(&out, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"");
    lsp_json_escape(&out, uri);
    lipi_str_append(&out, "\",\"diagnostics\":[]}}");
    lsp_send_json(out.data ? out.data : "{}");
    lipi_str_free(&out);
}

static void lsp_seed_open_documents(LspServer *server, LipiCompiler *c) {
    for (int i = 0; i < server->documents.len; i++) {
        LspDocument *doc = (LspDocument *)server->documents.items[i];
        lipi_source_from_text(c, doc->path, doc->text ? doc->text : "");
    }
}

static void lsp_check_document(LspServer *server, LspDocument *doc) {
    LipiCompiler c;
    lsp_compiler_init(server, &c);
    lsp_seed_open_documents(server, &c);
    if (lipi_load_program(&c, doc->path)) {
        if (lipi_expand_macros(&c)) {
            lipi_sema_check(&c);
        }
    }
    lsp_publish_diagnostics(doc, &c);
    lsp_compiler_cleanup(&c);
}

static void lsp_document_end(const char *text, int *line, int *character) {
    *line = 0;
    *character = 0;
    for (const char *p = text ? text : ""; *p; p++) {
        if (*p == '\n') {
            (*line)++;
            *character = 0;
        } else {
            (*character)++;
        }
    }
}

static void lsp_format_document(LspServer *server, const char *id, const char *uri) {
    LspDocument *doc = lsp_find_document(server, uri);
    if (!doc) {
        lsp_send_response(id, "[]");
        return;
    }

    LipiCompiler c;
    lsp_compiler_init(server, &c);
    LipiStr formatted = {0};
    int ok = lipi_format_source_text(&c, doc->path, doc->text ? doc->text : "", &formatted);
    if (!ok) {
        lsp_send_response(id, "[]");
        lsp_compiler_cleanup(&c);
        lipi_str_free(&formatted);
        return;
    }

    int end_line = 0;
    int end_char = 0;
    lsp_document_end(doc->text, &end_line, &end_char);
    LipiStr result = {0};
    lipi_str_appendf(&result,
                     "[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
                     "\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"",
                     end_line, end_char);
    lsp_json_escape(&result, formatted.data ? formatted.data : "");
    lipi_str_append(&result, "\"}]");
    lsp_send_response(id, result.data ? result.data : "[]");
    lipi_str_free(&result);
    lipi_str_free(&formatted);
    lsp_compiler_cleanup(&c);
}

static void lsp_handle_initialize(const char *id) {
    const char *result =
        "{\"capabilities\":{\"textDocumentSync\":1,\"documentFormattingProvider\":true},"
        "\"serverInfo\":{\"name\":\"lipi\",\"version\":\"0.1\"}}";
    lsp_send_response(id, result);
}

static void lsp_handle_message(LspServer *server, const char *body) {
    char *method = json_get_string(body, "method");
    char *id = json_get_id_raw(body);
    if (!method) {
        free(id);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        lsp_handle_initialize(id);
    } else if (strcmp(method, "initialized") == 0) {
        /* notification */
    } else if (strcmp(method, "shutdown") == 0) {
        server->shutdown = 1;
        lsp_send_response(id, "null");
    } else if (strcmp(method, "exit") == 0) {
        server->exiting = 1;
    } else if (strcmp(method, "textDocument/didOpen") == 0 ||
               strcmp(method, "textDocument/didChange") == 0 ||
               strcmp(method, "textDocument/didSave") == 0) {
        char *uri = json_get_string(body, "uri");
        char *text = json_get_string(body, "text");
        if (uri) {
            LspDocument *doc = NULL;
            if (text) {
                doc = lsp_upsert_document(server, uri, text);
            } else {
                doc = lsp_find_document(server, uri);
            }
            if (doc) {
                lsp_check_document(server, doc);
            }
        }
        free(uri);
        free(text);
    } else if (strcmp(method, "textDocument/didClose") == 0) {
        char *uri = json_get_string(body, "uri");
        if (uri) {
            lsp_close_document(server, uri);
            lsp_publish_empty_diagnostics(uri);
        }
        free(uri);
    } else if (strcmp(method, "textDocument/formatting") == 0) {
        char *uri = json_get_string(body, "uri");
        if (id && uri) {
            lsp_format_document(server, id, uri);
        } else if (id) {
            lsp_send_response(id, "[]");
        }
        free(uri);
    } else if (id) {
        lsp_send_error(id, -32601, "method not found");
    }

    free(method);
    free(id);
}

static void lsp_free_server(LspServer *server) {
    for (int i = 0; i < server->documents.len; i++) {
        LspDocument *doc = (LspDocument *)server->documents.items[i];
        free(doc->uri);
        free(doc->path);
        free(doc->text);
        free(doc);
    }
    free(server->documents.items);
}

int lipi_lsp_run(LipiCompiler *base) {
    setvbuf(stdout, NULL, _IONBF, 0);
    LspServer server;
    memset(&server, 0, sizeof(server));
    server.base = base;

    char *body = NULL;
    while (lsp_read_message(&body)) {
        lsp_handle_message(&server, body);
        free(body);
        body = NULL;
        if (server.exiting) {
            break;
        }
    }

    lsp_free_server(&server);
    return 0;
}
