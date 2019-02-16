#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#define _XOPEN_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <fcntl.h>

typedef struct str {
    unsigned char *start, *end;
    void *buf_start, *buf_end;
} str_t;

static inline str_t *new_str(void)
{
    str_t *str = malloc(sizeof(*str));
    if (!str)
        abort();
    str->start = str->end = NULL;
    str->buf_start = str->buf_end = NULL;
    return str;
}

static inline void free_str(const str_t *s)
{
    str_t *str = (void *)s;
    if (!str)
        return;
    free(str->buf_start);
    str->start = str->end = NULL;
    str->buf_start = str->buf_end = NULL;
    free(str);
}

static inline size_t str_len(const str_t *str)
{
    if (!str)
        return 0;
    return str->end - str->start;
}

static inline void str_clear(str_t *str)
{
    str->start = str->end = str->buf_start;
    if (str->start)
        *str->start = 0;
}

static inline str_t *dup_str(const str_t *str)
{
    str_t *new_str = malloc(sizeof(*str));
    size_t len = str_len(str);
    if (!new_str)
        abort();
    new_str->start = new_str->end = NULL;
    new_str->buf_start = new_str->buf_end = NULL;
    if (!len)
        return new_str;
    if (!(new_str->buf_start = malloc(len + 1)))
        abort();
    new_str->buf_end = (char *)new_str->buf_start + len;
    new_str->start = new_str->buf_start;
    new_str->end = new_str->buf_end;
    memcpy(new_str->start, str->start, len);
    new_str->start[len] = 0;
    return new_str;
}

static inline int str_eq(const str_t *a, const str_t *b)
{
    size_t len = str_len(b);
    if (len != str_len(a))
        return 0;
    return !memcmp(a->start, b->start, len);
}

static inline int str_ceq(const str_t *a, const char *b)
{
    size_t len = strlen(b);
    if (len != str_len(a))
        return 0;
    return !memcmp(a->start, b, len);
}

static inline int str_empty(const str_t *s)
{
    return !str_len(s);
}

static void str_reserve(str_t *str, size_t size)
{
    unsigned char *ptr;
    size_t buf_size, avail_size, req_size, offset;
    avail_size = (size_t)((unsigned char *)str->buf_end - str->end);
    offset = str->start - (unsigned char *)str->buf_start;
    buf_size = (char *)str->buf_end - (char *)str->buf_start;
    if (size <= avail_size)
        return;
    if (size <= avail_size + offset) {
        memmove(str->start - offset, str->start, str_len(str) + 1);
        str->start -= offset;
        str->end -= offset;
        return;
    }
    if (buf_size > SIZE_MAX - size)
        abort();
    req_size = buf_size + size;
    if (buf_size == 0)
        buf_size = 16;
    while (buf_size < req_size) {
        if (buf_size >= SIZE_MAX / 2)
            abort();
        buf_size *= 2;
    }
    ptr = realloc(str->buf_start, buf_size);
    assert(ptr);
    if (!ptr)
        abort();
    str->start = ptr + (str->start - (unsigned char *)str->buf_start);
    str->end = ptr + (str->end - (unsigned char *)str->buf_start);
    str->buf_start = ptr;
    str->buf_end = ptr + buf_size;
}

static inline void str_putc(str_t *str, int c)
{
    str_reserve(str, 2);
    *str->end++ = c;
    *str->end = 0;
}

static inline void str_put(str_t *str, const void *data, size_t size)
{
    str_reserve(str, size);
    memcpy(str->end, data, size);
    str->end += size;
    str_reserve(str, 1);
    *str->end = 0;
}

static inline int str_getc(str_t *str)
{
    if (str->start >= str->end)
        return EOF;
    return *str->start++;
}

enum word_type {
    WORD_STRING,
    WORD_PARAMETER,
    WORD_COMMAND,
    WORD_ARITHMETIC,
    WORD_BACKTICK,
};

typedef struct word_part {
    enum word_type type;
    struct word_part *next;
    int quoted, was_quoted;
    str_t *tok;
} word_t;

static void free_word(word_t *part)
{
    word_t *next;
    while (part) {
        next = part->next;
        free_str(part->tok);
        free(part);
        part = next;
    }
}

enum tok {
    TOK_EOF = -1,

    // Tokens
    TOK_WORD, TOK_ASSIGNMENT_WORD, TOK_NAME, TOK_NEWLINE, TOK_IO_NUMBER,

    // Operators
    TOK_AND, TOK_PIPE, TOK_LESS, TOK_GREAT, TOK_SEMI, TOK_LPAREN, TOK_RPAREN,
    TOK_AND_IF, TOK_OR_IF, TOK_DSEMI, TOK_DLESS, TOK_DGREAT, TOK_LESSAND,
    TOK_GREATAND, TOK_LESSGREAT, TOK_DLESSDASH, TOK_CLOBBER,

    // Reserved words
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_ELIF, TOK_FI, TOK_DO, TOK_DONE,
    TOK_CASE, TOK_ESAC, TOK_WHILE, TOK_UNTIL, TOK_FOR,
    TOK_LBRACE, TOK_RBRACE, TOK_BANG, TOK_IN,
};

char *stok(enum tok type)
{
    switch (type) {
#define T(t) case t: return #t;
    T(TOK_EOF)
    T(TOK_WORD)
    T(TOK_ASSIGNMENT_WORD)
    T(TOK_NAME)
    T(TOK_NEWLINE)
    T(TOK_IO_NUMBER)
    T(TOK_AND)
    T(TOK_PIPE)
    T(TOK_LESS)
    T(TOK_GREAT)
    T(TOK_SEMI)
    T(TOK_LPAREN)
    T(TOK_RPAREN)
    T(TOK_AND_IF)
    T(TOK_OR_IF)
    T(TOK_DSEMI)
    T(TOK_DLESS)
    T(TOK_DGREAT)
    T(TOK_LESSAND)
    T(TOK_GREATAND)
    T(TOK_LESSGREAT)
    T(TOK_DLESSDASH)
    T(TOK_CLOBBER)
    T(TOK_IF)
    T(TOK_THEN)
    T(TOK_ELSE)
    T(TOK_ELIF)
    T(TOK_FI)
    T(TOK_DO)
    T(TOK_DONE)
    T(TOK_CASE)
    T(TOK_ESAC)
    T(TOK_WHILE)
    T(TOK_UNTIL)
    T(TOK_FOR)
    T(TOK_LBRACE)
    T(TOK_RBRACE)
    T(TOK_BANG)
    T(TOK_IN)
#undef T
    default:
        return "(TOK_?)";
    }
}

struct heredoc {
    struct heredoc *next;
    int refs, is_valid;
    str_t *end;
    str_t *doc;
};

struct lexer {
    enum tok type, saved_type;
    int quoted, has_token, backslash, is_op, was_quoted, errored;
    str_t *tok, *src;
    struct heredoc *heredoc, **heredoc_link;
    word_t *word, **word_end;
    int has_ungot, ungotch;
};

static void lex_link_part(struct lexer *lex, enum word_type type)
{
    word_t *word = malloc(sizeof(*word));
    if (!word)
        abort();
    word->next = NULL;
    word->type = type;
    word->quoted = lex->quoted;
    word->was_quoted = lex->was_quoted;
    word->tok = dup_str(lex->tok);
    str_clear(lex->tok);
    *lex->word_end = word;
    lex->word_end = &word->next;
}

static word_t *lex_take_word(struct lexer *lex)
{
    word_t *word = lex->word;
    if (!word)
        return NULL;
    lex->word = NULL;
    lex->word_end = &lex->word;
    return word;
}

static void init_lex(struct lexer *lex, str_t *src)
{
    lex->type = TOK_EOF;
    lex->saved_type = TOK_EOF;
    lex->quoted = 0;
    lex->has_token = 0;
    lex->backslash = 0;
    lex->is_op = 0;
    lex->errored = 0;
    lex->was_quoted = 0;
    lex->tok = new_str();
    lex->word = NULL;
    lex->word_end = &lex->word;
    lex->heredoc = NULL;
    lex->heredoc_link = &lex->heredoc;
    lex->has_ungot = 0;
    lex->ungotch = 0;
    lex->src = src ? dup_str(src) : NULL;
}

static void free_doc(struct heredoc *doc)
{
    if (!doc || --doc->refs)
        return;
    assert(!doc->next);
    free_str(doc->end);
    free_str(doc->doc);
    free(doc);
}

static void destroy_lex(struct lexer *lex)
{
    struct heredoc *doc, *next;
    free_str(lex->tok);
    free_word(lex->word);
    free_str(lex->src);
    for (doc = lex->heredoc; doc; doc = next) {
        next = doc->next;
        doc->next = NULL;
        doc->is_valid = 1;
        free_doc(doc);
    }
}

static int lex_getc(struct lexer *lex)
{
    if (lex->has_ungot) {
        lex->has_ungot = 0;
        return lex->ungotch;
    }
    if (lex->src)
        return str_getc(lex->src);
    return getc(stdin);
}

static void lex_ungetc(struct lexer *lex, int ch)
{
    if (ch < 0)
        return;
    lex->has_ungot = 1;
    lex->ungotch = ch;
}

struct tok_def {
    const char *tok;
    enum tok type;
};

static const struct tok_def ops[] = {
    {"&", TOK_AND},
    {"|", TOK_PIPE},
    {"<", TOK_LESS},
    {">", TOK_GREAT},
    {";", TOK_SEMI},
    {"(", TOK_LPAREN},
    {")", TOK_RPAREN},
    {"&&", TOK_AND_IF},
    {"||", TOK_OR_IF},
    {";;", TOK_DSEMI},
    {"<<", TOK_DLESS},
    {">>", TOK_DGREAT},
    {"<&", TOK_LESSAND},
    {">&", TOK_GREATAND},
    {"<>", TOK_LESSGREAT},
    {"<<-", TOK_DLESSDASH},
    {">|", TOK_CLOBBER},
    {NULL, TOK_EOF},
};

static int is_opstart(int ch)
{
    const struct tok_def *def;
    for (def = ops; def->tok; def++)
        if (ch == *def->tok)
            return 1;
    return 0;
}

static int tok_build_op(struct lexer *lex, int ch)
{
    const struct tok_def *def;
    char op[4] = {0}, end[2] = {ch, 0};
    if (ch == 0)
        return 0;
    if (!str_empty(lex->tok))
        strcat(op, (void *)lex->tok->start);
    strcat(op, end);
    for (def = ops; def->tok; def++) {
        if (!strcmp(def->tok, op)) {
            str_putc(lex->tok, ch);
            lex->type = def->type;
            return 1;
        }
    }
    return 0;
}

void unget_tok(struct lexer *lex, enum tok type)
{
    lex->saved_type = type;
}

static int is_number(const str_t *s)
{
    const unsigned char *tok;
    if (!s || str_empty(s))
        return 0;
    for (tok = s->start; tok < s->end; tok++)
        if (*tok < '0' || *tok > '9' || !*tok)
            return 0;
    return 1;
}

static inline long parse_number(str_t *s, int base, int *err)
{
    int saved_errno = errno;
    long val;
    if (!is_number(s))
        goto error;
    errno = 0;
    val = strtol((void *)s->start, NULL, base);
    if (errno)
        goto error;
    if (err)
        *err = 0;
    errno = saved_errno;
    return val;
error:
    if (err)
        *err = 1;
    errno = saved_errno;
    return LONG_MIN;
}

static int is_name(const str_t *s)
{
    const unsigned char *tok;
    if (!s || str_empty(s))
        return 0;
    tok = s->start;
    if (*tok >= '0' && *tok <= '9')
        return 0;
    for (; tok < s->end; tok++) {
        if (*tok == '_')
            continue;
        if (*tok >= 'a' && *tok <= 'z')
            continue;
        if (*tok >= 'A' && *tok <= 'Z')
            continue;
        if (*tok >= '0' && *tok <= '9')
            continue;
        return 0;
    }
    return 1;
}

static int is_assignment(const str_t *s)
{
    const unsigned char *tok;
    if (!s || str_empty(s))
        return 0;
    tok = s->start;
    if (*tok >= '0' && *tok <= '9')
        return 0;
    for (; tok < s->end; tok++) {
        if (*tok == '_')
            continue;
        if (*tok >= 'a' && *tok <= 'z')
            continue;
        if (*tok >= 'A' && *tok <= 'Z')
            continue;
        if (*tok >= '0' && *tok <= '9')
            continue;
        if (*tok == '=')
            break;
        return 0;
    }
    if (tok == s->end)
        return 0;
    return 1;
}

struct tok_def res_words[] = {
    {"!", TOK_BANG},
    {"{", TOK_LBRACE},
    {"}", TOK_RBRACE},
    {"case", TOK_CASE},
    {"do", TOK_DO},
    {"done", TOK_DONE},
    {"elif", TOK_ELIF},
    {"else", TOK_ELSE},
    {"esac", TOK_ESAC},
    {"fi", TOK_FI},
    {"for", TOK_FOR},
    {"if", TOK_IF},
    {"in", TOK_IN},
    {"then", TOK_THEN},
    {"until", TOK_UNTIL},
    {"while", TOK_WHILE},
    {NULL, TOK_EOF},
};

int is_tok_name(enum tok tok)
{
    switch (tok) {
    case TOK_CASE: case TOK_DO: case TOK_DONE: case TOK_ELIF: case TOK_ELSE:
    case TOK_ESAC: case TOK_FI: case TOK_FOR: case TOK_IF: case TOK_IN:
    case TOK_THEN: case TOK_UNTIL: case TOK_WHILE: case TOK_NAME:
        return 1;
    default:
        return 0;
    }
}

int is_tok_word(enum tok tok)
{
    return tok == TOK_BANG || tok == TOK_LBRACE || tok == TOK_RBRACE ||
           tok == TOK_WORD || tok == TOK_ASSIGNMENT_WORD || is_tok_name(tok);
}

static enum tok match_reserved(const str_t *tok)
{
    struct tok_def *def;
    for (def = res_words; def->tok; def++)
        if (str_ceq(tok, def->tok))
            return def->type;
    return TOK_EOF;
}

static void tok_rec(struct lexer *lex, int ch, enum word_type type)
{
    str_t *tok;
    enum tok res;

    if (!str_empty(lex->tok))
        lex_link_part(lex, type);

    lex->has_token = 1;

    if (!lex->word)
        return;

    tok = lex->word->tok;

    if (lex->was_quoted || lex->word->next || lex->word->type != WORD_STRING) {
        if (is_assignment(tok))
            lex->type = TOK_ASSIGNMENT_WORD;
        return;
    }

    if (is_assignment(tok))
        lex->type = TOK_ASSIGNMENT_WORD;

    if ((ch == '<' || ch == '>') && is_number(tok))
        lex->type = TOK_IO_NUMBER;
    res = match_reserved(tok);
    if (res == TOK_EOF && is_name(tok))
        lex->type = TOK_NAME;
    if (res != TOK_EOF)
        lex->type = res;
}

int lex_errored(struct lexer *lex)
{
    return lex->errored;
}

void syntax_errorv(struct lexer *lex, char *fmt, va_list args)
{
    vprintf(fmt, args);
    lex->errored = 1;
}

void syntax_error(struct lexer *lex, char *fmt, ...)
{
    va_list args;
    if (lex->errored)
        return;
    va_start(args, fmt);
    syntax_errorv(lex, fmt, args);
    va_end(args);
}

void lex_reset_skip_line(struct lexer *lex)
{
    struct heredoc *doc, *next;
    int c;
    while ((c = lex_getc(lex)) != '\n')
        if (c == -1)
            break;
    lex->saved_type = TOK_EOF;
    lex->type = TOK_EOF;
    lex->has_token = 1;
    lex->errored = 0;
    for (doc = lex->heredoc; doc; doc = next) {
        next = doc->next;
        doc->next = NULL;
        doc->is_valid = 1;
        free_doc(doc);
    }
    lex->heredoc = NULL;
    lex->heredoc_link = &lex->heredoc;
}

static void read_heredoc(struct lexer *lex, struct heredoc *doc)
{
    int ch;
    unsigned char *cur;

    while ((ch = lex_getc(lex)) != '\n')
        if (ch < 0)
            return;

    while (1) {
        for (cur = doc->end->start; cur < doc->end->end; cur++) {
            ch = lex_getc(lex);
            if (ch < 0)
                return;
            if (ch != *cur) {
                lex_ungetc(lex, ch);
                break;
            }
        }

        if (cur == doc->end->end) {
            ch = lex_getc(lex);
            lex_ungetc(lex, ch);
            if (ch < 0 || ch == '\n')
                return;
        }

        str_put(doc->doc, doc->end->start, cur - doc->end->start);

        do {
            ch = lex_getc(lex);
            if (ch < 0)
                return;
            str_putc(doc->doc, ch);
        } while (ch != '\n');
    }
}

static void lex_read_heredocs(struct lexer *lex)
{
    struct heredoc *doc;
    while ((doc = lex->heredoc)) {
        read_heredoc(lex, doc);
        doc->is_valid = 1;
        if (!(lex->heredoc = doc->next))
            lex->heredoc_link = &lex->heredoc;
        doc->next = NULL;
        free_doc(doc);
    }
}

enum tok get_tok(struct lexer *lex)
{
    int ch;

    if (lex->errored)
        return TOK_EOF;

    if (lex->saved_type != TOK_EOF) {
        lex->type = lex->saved_type;
        lex->saved_type = TOK_EOF;
        lex->has_token = 1;
        return lex->type;
    }

    if (lex->has_token) {
        lex->type = TOK_EOF;
        lex->has_token = 0;
        lex->was_quoted = 0;
        str_clear(lex->tok);
        free_word(lex->word);
        lex->word = NULL;
        lex->word_end = &lex->word;
    }

    while (1) {
        ch = lex_getc(lex);

        // rule 1 - end of input
        if (ch == EOF) {
            tok_rec(lex, 0, WORD_STRING);
            return lex->type;
        }

        if (lex->is_op) {
            // rule 2 - try to form an operator
            if (tok_build_op(lex, ch))
                continue;

            // rule 3 - finish an operator
            if (ch == '\\') {
                ch = lex_getc(lex);
                if (ch == '\n')
                    continue;
                lex->backslash = 1;
            }
            assert(lex->type != TOK_EOF);
            lex_ungetc(lex, ch);
            lex->is_op = 0;
            tok_rec(lex, 0, WORD_STRING);
            return lex->type;
        }

        // rule 4 - quoting
        if (lex->backslash || ch == '\\') {
            if (!lex->backslash)
                ch = lex_getc(lex);
            lex->backslash = 0;
            if (ch == '\n')
                continue;
            lex->was_quoted = 1;
            if (lex->quoted && (ch == '$' || ch == '`' ||
                        ch == '"' || ch == '\\' ||
                        ch == '\n')) {
                lex_ungetc(lex, ch);
                lex->type = TOK_WORD;
                str_putc(lex->tok, '\\');
                continue;
            }
            lex->type = TOK_WORD;
            str_putc(lex->tok, ch);
            continue;
        }

        if (!lex->quoted && ch == '\'') {
            lex->was_quoted = 1;
            lex->type = TOK_WORD;
            while (1) {
                ch = lex_getc(lex);
                if (ch == '\'')
                    break;
                str_putc(lex->tok, ch);
            }
            continue;
        }

        if (ch == '"') {
            if (lex->type == TOK_EOF)
                lex->type = TOK_WORD;
            lex->was_quoted = 1;
            lex->quoted = !lex->quoted;
            continue;
        }


        // rule 5 - substitution
        if (ch == '$') {
            //TODO: Arthmetic exprs, etc...
            if (!str_empty(lex->tok))
                lex_link_part(lex, WORD_STRING);
            while (1) {
                ch = lex_getc(lex);
                if (ch == '\\') {
                    ch = lex_getc(lex);
                    if (ch == '\n')
                        continue;
                    lex_ungetc(lex, ch);
                    lex->backslash = 1;
                    break;
                }
                if (!((ch >= 'A' && ch <= 'Z') ||
                        (ch >= 'a' && ch <= 'z') ||
                        (!str_empty(lex->tok) && ch >= '0' && ch <= '9'))) {
                    lex_ungetc(lex, ch);
                    break;
                }
                str_putc(lex->tok, ch);
            }
            lex->type = TOK_WORD;
            if (!str_empty(lex->tok)) {
                lex_link_part(lex, WORD_PARAMETER);
            } else {
                str_putc(lex->tok, '$');
            }
            continue;
        }

        //TODO backtick

        // rule 6 - start of an operator
        if (!lex->quoted && is_opstart(ch)) {
            if (lex->type != TOK_EOF) {
                lex_ungetc(lex, ch);
                tok_rec(lex, ch, WORD_STRING);
                return lex->type;
            }
            lex_ungetc(lex, ch);
            lex->is_op = 1;
            continue;
        }

        // rule 7 - new line
        if (!lex->quoted && ch == '\n') {
            if (lex->type != TOK_EOF) {
                lex_ungetc(lex, ch);
                tok_rec(lex, 0, WORD_STRING);
                return lex->type;
            } else {
                if (lex->heredoc) {
                    lex_ungetc(lex, ch);
                    lex_read_heredocs(lex);
                    continue;
                }
                lex->type = TOK_NEWLINE;
                str_putc(lex->tok, ch);
                tok_rec(lex, 0, WORD_STRING);
                return lex->type;
            }
        }

        // rule 8 - space
        if (!lex->quoted && isspace(ch)) {
            if (lex->type != TOK_EOF) {
                tok_rec(lex, 0, WORD_STRING);
                return lex->type;
            }
            continue;
        }

        // rule 9 - word
        if (lex->type == TOK_WORD) {
            str_putc(lex->tok, ch);
            continue;
        }

        // rule 10 - comment
        if (!lex->quoted && ch == '#') {
            while (1) {
                ch = lex_getc(lex);
                if (ch == '\n' || ch == EOF)
                    break;
            }
            lex_ungetc(lex, ch);
            continue;
        }

        // rule 11 - start of a word
        lex->type = TOK_WORD;
        str_putc(lex->tok, ch);
    }
}

int lex_accept(struct lexer *lex, enum tok expected)
{
    enum tok tok = get_tok(lex);
    if (expected == TOK_NAME && is_tok_name(tok))
        return 1;
    if (expected == TOK_WORD && is_tok_word(tok))
        return 1;
    if (tok == expected)
        return 1;
    unget_tok(lex, tok);
    return 0;
}

int lex_peek(struct lexer *lex, enum tok expected)
{
    enum tok tok = get_tok(lex);
    unget_tok(lex, tok);
    if (expected == TOK_NAME && is_tok_name(tok))
        return 1;
    if (expected == TOK_WORD && is_tok_word(tok))
        return 1;
    if (tok == expected)
        return 1;
    return 0;
}

str_t *lex_accept_single_word(struct lexer *lex, enum tok tok)
{
    word_t *word;
    str_t *str;
    if (!lex_accept(lex, tok))
        return NULL;
    word = lex_take_word(lex);
    if (word->next || word->quoted || word->type != WORD_STRING) {
        free_word(word);
        return NULL;
    }
    str = dup_str(word->tok);
    free_word(word);
    return str;
}

word_t *lex_accept_word(struct lexer *lex)
{
    enum tok tok = get_tok(lex);
    if (!is_tok_word(tok)) {
        unget_tok(lex, tok);
        return NULL;
    }
    return lex_take_word(lex);
}

word_t *lex_accept_assignment(struct lexer *lex)
{
    enum tok tok = get_tok(lex);
    if (tok != TOK_ASSIGNMENT_WORD) {
        unget_tok(lex, tok);
        return NULL;
    }
    return lex_take_word(lex);
}

struct redirect {
    struct redirect *next;
    int fd;
    enum tok op;
    word_t *name;
    struct heredoc *doc;
};

struct heredoc *lex_start_heredoc(struct lexer *lex)
{
    struct heredoc *doc;
    word_t *first;
    if (!lex_peek(lex, TOK_WORD))
        return NULL;

    first = lex->word;
    if (!first || first->type != WORD_STRING)
        return NULL;

    lex->word = first->next;
    if (!first->next)
        lex->word_end = &lex->word;
    first->next = NULL;

    doc = malloc(sizeof(*doc));
    if (!doc)
        abort();
    doc->next = NULL;
    doc->refs = 2;
    doc->is_valid = 0;
    doc->end = dup_str(first->tok);
    free_word(first);
    doc->doc = new_str();
    *lex->heredoc_link = doc;
    lex->heredoc_link = &doc->next;
    return doc;
}

static struct redirect *parse_redirect(struct lexer *lex)
{
    struct redirect *re;
    struct heredoc *doc = NULL;
    word_t *name = NULL;
    str_t *num;
    enum tok tok;
    long fd = -1;
    int must_match = 0, err = 0;

    if ((num = lex_accept_single_word(lex, TOK_IO_NUMBER))) {
        fd = parse_number(num, 10, &err);
        if (err || fd < 0 || fd > INT_MAX) {
            syntax_error(lex, "Bad IO_NUMBER\n");
            return NULL;
        }
        return NULL;
    }

    tok = get_tok(lex);
    switch (tok) {
    case TOK_LESS:
    case TOK_LESSAND:
    case TOK_LESSGREAT:
    case TOK_DLESS:
    case TOK_DLESSDASH:
        if (fd < 0)
            fd = 0;
        break;
    case TOK_GREAT:
    case TOK_GREATAND:
    case TOK_DGREAT:
    case TOK_CLOBBER:
        if (fd < 0)
            fd = 1;
        break;
    default:
        if (!must_match) {
            unget_tok(lex, tok);
            return NULL;
        }
        syntax_error(lex, "Bad redirection\n");
        return NULL;
    }

    if (tok == TOK_DLESS || tok == TOK_DLESSDASH) {
        doc = lex_start_heredoc(lex);
        if (!doc) {
            syntax_error(lex, "Bad redirection: expected heredoc\n");
            return NULL;
        }
    } else {
        name = lex_accept_word(lex);
        if (!name) {
            syntax_error(lex, "Bad redirection: expected filename\n");
            return NULL;
        }
    }

    re = malloc(sizeof(*re));
    if (!re)
        abort();
    re->next = NULL;
    re->fd = fd;
    re->op = tok;
    re->name = name;
    re->doc = doc;

    return re;
}

struct var {
    struct var *next;
    str_t *name;
    word_t *val;
};

struct arg {
    struct arg *next;
    word_t *val;
};

enum cmd_type {
    CMD_SIMPLE,
    CMD_ASSIGNMENT,
    CMD_ANDOR,
    CMD_PIPELINE,
    CMD_COMPOUND,
    CMD_SUBSHELL,
    CMD_LOOP,
    CMD_COND,
    CMD_REDIRS,
    CMD_FOR_LOOP,
    CMD_FUNCTION,
    CMD_CASES,
};

struct cmd_base {
    enum cmd_type type;
    int refs;
};

struct cmd {
    struct cmd_base base;
    struct redirect *redirs;
    struct arg *args;
    struct var *vars;
    int background;
};

struct andor {
    struct cmd_base base;
    int negated, and;
    struct andor *next;
    union node *command;
};

struct pipeline {
    struct cmd_base base;
    struct pipeline *next;
    union node *command;
    int background;
};

struct compound {
    struct cmd_base base;
    struct compound *next;
    union node *command;
};

struct subshell {
    struct cmd_base base;
    int background;
    union node *commands;
};

struct loop {
    struct cmd_base base;
    int until;
    union node *cond;
    union node *commands;
};

struct cond {
    struct cmd_base base;
    union node *cond;
    union node *commands;
    union node *otherwise;
};

struct redirs {
    struct cmd_base base;
    struct redirect *redirs;
    union node *command;
};

struct item {
    struct item *next;
    word_t *val;
};

struct for_loop {
    struct cmd_base base;
    str_t *name;
    int use_args;
    struct item *items;
    union node *command;
};

struct function {
    struct cmd_base base;
    str_t *name;
    union node *command;
};

struct cases {
    struct cmd_base base;
};

typedef union node {
    enum cmd_type type;
    struct cmd_base base;
#define node_next base.next
    struct cmd simp;
    struct andor andor;
    struct pipeline pipe;
    struct compound comp;
    struct subshell sub;
    struct loop loop;
    struct cond cond;
    struct redirs redirs;
    struct for_loop for_loop;
    struct function func;
    struct cases cases;
} node_t;

static node_t *alloc_node(enum cmd_type type)
{
    node_t *c = malloc(sizeof(*c));
    if (!c)
        abort();
    memset(c, 0, sizeof(*c));
    c->base.type = type;
    c->base.refs = 1;
    return c;
}

static node_t *ref_node(node_t *node)
{
    node->base.refs++;
    return node;
}

static void free_redirects(struct redirect *redirects)
{
    struct redirect *r, *nr;
    for (r = redirects; r; r = nr) {
        nr = r->next;
        free_word(r->name);
        free_doc(r->doc);
        free(r);
    }
}

static void free_items(struct item *items)
{
    struct item *next;
    while (items) {
        next = items->next;
        free_word(items->val);
        free(items);
        items = next;
    }
}

static void free_args(struct arg *args)
{
    struct arg *next;
    while (args) {
        next = args->next;
        free_word(args->val);
        free(args);
        args = next;
    }
}

static void free_vars(struct var *vars)
{
    struct var *next;
    while (vars) {
        next = vars->next;
        free_str(vars->name);
        free_word(vars->val);
        free(vars);
        vars = next;
    }
}

static void free_node(node_t *node)
{
    node_t *next = NULL;
    for (; node && !--node->base.refs; node = next) {
        switch (node->type) {
        case CMD_SIMPLE: case CMD_ASSIGNMENT:
            free_redirects(node->simp.redirs);
            free_args(node->simp.args);
            free_vars(node->simp.vars);
            free(node);
            next = NULL;
            break;
        case CMD_ANDOR:
            free_node(node->andor.command);
            next = (node_t *)node->andor.next;
            free(node);
            break;
        case CMD_PIPELINE:
            free_node(node->pipe.command);
            next = (node_t *)node->pipe.next;
            free(node);
            break;
        case CMD_COMPOUND:
            free_node(node->comp.command);
            next = (node_t *)node->comp.next;
            free(node);
            break;
        case CMD_SUBSHELL:
            next = node->sub.commands;
            free(node);
            break;
        case CMD_LOOP:
            free_node(node->loop.cond);
            next = node->loop.commands;
            free(node);
            break;
        case CMD_COND:
            free_node(node->cond.cond);
            free_node(node->cond.commands);
            next = node->cond.otherwise;
            free(node);
            break;
        case CMD_REDIRS:
            free_redirects(node->redirs.redirs);
            next = node->redirs.command;
            free(node);
            break;
        case CMD_FUNCTION:
            free_str(node->func.name);
            next = node->func.command;
            free(node);
            break;
        case CMD_FOR_LOOP:
            free_str(node->for_loop.name);
            next = node->for_loop.command;
            free_items(node->for_loop.items);
            free(node);
            break;
        case CMD_CASES:
            //TODO
            free(node);
            next = NULL;
            break;
        }
    }
}

static void link_arg(struct arg ***aptr, word_t *arg)
{
    struct arg *a = malloc(sizeof(*a));
    if (!a)
        abort();
    a->next = NULL;
    a->val = arg;
    **aptr = a;
    *aptr = &a->next;
}

static void link_var(struct var ***vptr, word_t *var)
{
    struct var *v = malloc(sizeof(*v));
    str_t name, val, *new_val;
    unsigned char *eq;
    if (!v)
        abort();
    eq = memchr(var->tok->start, '=', str_len(var->tok));
    if (!eq)
        abort();
    name.start = name.buf_start = var->tok->start;
    name.end = name.buf_end = eq;
    val.start = val.buf_start = eq + 1;
    val.end = val.buf_end = var->tok->end;
    v->next = NULL;
    v->name = dup_str(&name);
    new_val = dup_str(&val);
    free_str(var->tok);
    var->tok = new_val;
    v->val = var;
    **vptr = v;
    *vptr = &v->next;
}

static void link_redirect(struct redirect ***rptr, struct redirect *redir)
{
    **rptr = redir;
    *rptr = &redir->next;
}

static node_t *wrap_redirs(node_t *node, struct redirect *r)
{
    struct redirect **link;
    node_t *wrapped;
    if (!r)
        return node;
    if (!node) {
        free_redirects(r);
        return NULL;
    }
    if (node->type == CMD_REDIRS || node->type == CMD_SIMPLE ||
            node->type == CMD_ASSIGNMENT) {
        if (node->type == CMD_REDIRS)
            link = &node->redirs.redirs;
        else
            link = &node->simp.redirs;
        while (*link)
            link = &(*link)->next;
        *link = r;
        return node;
    }
    wrapped = alloc_node(CMD_REDIRS);
    wrapped->redirs.command = node;
    wrapped->redirs.redirs = r;
    return wrapped;
}

node_t *parse_compound(struct lexer *lex);
node_t *parse_single(struct lexer *lex);

word_t *wrap_word(str_t *str)
{
    word_t *word = malloc(sizeof(*word));
    if (!word)
        abort();
    word->type = WORD_STRING;
    word->quoted = 0;
    word->next = NULL;
    word->tok = dup_str(str);
    return word;
}

static node_t *parse_simple_command(struct lexer *lex)
{
    node_t *node = alloc_node(CMD_SIMPLE);
    struct cmd *cmd = &node->simp;
    node_t *body = NULL;
    word_t *word = NULL;
    str_t *name = NULL;
    struct redirect *r;
    struct redirect *rlist = NULL, **rptr = &rlist;
    struct arg **aptr = &cmd->args;
    struct var **vptr = &cmd->vars;

    if (!lex_peek(lex, TOK_ASSIGNMENT_WORD) && (name = lex_accept_single_word(lex, TOK_NAME))) {
        if (lex_accept(lex, TOK_LPAREN)) {
            if (!lex_accept(lex, TOK_RPAREN)) {
                syntax_error(lex, "expected )\n");
                goto error;
            }
            while (lex_accept(lex, TOK_NEWLINE));
            body = parse_single(lex);
            if (!body) {
                syntax_error(lex, "Expected function body\n");
                goto error;
            }
            node->type = CMD_FUNCTION;
            node->func.name = name;
            node->func.command = body;
            return node;
        } else {
            link_arg(&aptr, wrap_word(name));
            free_str(name);
            name = NULL;
        }
    } else {
        while (1) {
            if ((r = parse_redirect(lex))) {
                link_redirect(&rptr, r);
                continue;
            } else  if ((word = lex_accept_assignment(lex))) {
                link_var(&vptr, word);
                continue;
            } else {
                if ((word = lex_accept_word(lex)))
                    link_arg(&aptr, word);
                break;
            }
        }
    }

    while (1) {
        if ((r = parse_redirect(lex))) {
            link_redirect(&rptr, r);
        } else if ((word = lex_accept_word(lex))) {
            link_arg(&aptr, word);
        } else {
            if (!cmd->args && !cmd->vars)
                goto error;
            break;
        }
    }

    if (!cmd->args) {
        cmd->base.type = CMD_ASSIGNMENT;
        if (rlist) {
            free_redirects(rlist);
            rlist = NULL;
        }
    }

    return wrap_redirs((void *)cmd, rlist);

error:
    free_str(name);
    free_redirects(rlist);
    free_node(node);
    return NULL;
}

static void link_item(struct item ***iptr, word_t *word)
{
    struct item *item = malloc(sizeof(*item));
    if (!item)
        abort();
    item->val = word;
    item->next = NULL;
    **iptr = item;
    *iptr = &item->next;
}

node_t *parse_for(struct lexer *lex)
{
    node_t *node = NULL, *body = NULL;
    str_t *name = NULL;
    word_t *word = NULL;
    int use_args = 0;
    struct item *items = NULL, **iptr = &items;
    if (!lex_accept(lex, TOK_FOR))
        goto error;

    if (!(name = lex_accept_single_word(lex, TOK_NAME))) {
        syntax_error(lex, "Expected for variable name\n");
        goto error;
    }

    while (lex_accept(lex, TOK_NEWLINE));

    if (lex_accept(lex, TOK_IN)) {
        while ((word = lex_accept_word(lex)))
            link_item(&iptr, word);
        if (!lex_accept(lex, TOK_SEMI) && !lex_accept(lex, TOK_NEWLINE)) {
            syntax_error(lex, "Expected ;\n");
            goto error;
        }
        while (lex_accept(lex, TOK_NEWLINE));
    } else {
        use_args = 1;
    }

    if (!lex_accept(lex, TOK_DO)) {
        syntax_error(lex, "Expected do\n");
        goto error;
    }

    body = parse_compound(lex);
    if (!body) {
        syntax_error(lex, "Expected body\n");
        goto error;
    }

    if (!lex_accept(lex, TOK_DONE)) {
        syntax_error(lex, "Expected done\n");
        goto error;
    }

    node = alloc_node(CMD_FOR_LOOP);
    node->for_loop.name = name;
    node->for_loop.use_args = use_args;
    node->for_loop.items = items;
    node->for_loop.command = body;
    return node;

error:
    free_str(name);
    free_items(items);
    free_node(body);
    return NULL;
}

node_t *parse_case(struct lexer *lex)
{
    syntax_error(lex, "TODO case\n");
    return NULL;
}

node_t *parse_loop(struct lexer *lex)
{
    int is_until;
    node_t *node;
    node_t *cond;
    node_t *cmds;
    if (!(is_until = lex_accept(lex, TOK_UNTIL)) && !lex_accept(lex, TOK_WHILE))
        return NULL;

    cond = parse_compound(lex);
    if (!cond) {
        syntax_error(lex, "Expected commands\n");
        return NULL;
    }

    if (!lex_accept(lex, TOK_DO)) {
        syntax_error(lex, "Expected do\n");
        free_node(cond);
        return NULL;
    }

    cmds = parse_compound(lex);
    if (!cmds) {
        syntax_error(lex, "Expected commands\n");
        free_node(cmds);
        return NULL;
    }


    if (!lex_accept(lex, TOK_DONE)) {
        syntax_error(lex, "Expected done\n");
        free_node(cond);
        free_node(cmds);
        return NULL;
    }

    node = alloc_node(CMD_LOOP);
    node->loop.until = is_until;
    node->loop.cond = cond;
    node->loop.commands = cmds;
    return node;
}

node_t *parse_cond(struct lexer *lex)
{
    node_t *node, *cond, *body;
    node_t *root = NULL, **eptr = &root;

    if (!lex_accept(lex, TOK_IF))
        return NULL;

    while (1) {
        cond = parse_compound(lex);
        if (!cond) {
            syntax_error(lex, "Expected condition\n");
            goto error;
        }

        if (!lex_accept(lex, TOK_THEN)) {
            syntax_error(lex, "Expected then\n");
            goto error;
        }

        body = parse_compound(lex);
        if (!body) {
            syntax_error(lex, "Expected body\n");
            goto error;
        }

        node = alloc_node(CMD_COND);
        node->cond.cond = cond;
        node->cond.commands = body;
        *eptr = node;
        eptr = &node->cond.otherwise;

        if (lex_accept(lex, TOK_ELSE)) {
            body = parse_compound(lex);
            if (!body) {
                syntax_error(lex, "Expected else block\n");
                goto error;
            }
            *eptr = body;
            break;
        }

        if (!lex_accept(lex, TOK_ELIF))
            break;
    }

    if (!lex_accept(lex, TOK_FI)) {
        syntax_error(lex, "Expected fi");
        goto error;
    }

    return root;

error:
    free_node(root);
    return NULL;
}

node_t *parse_subshell(struct lexer *lex)
{
    node_t *sub, *node;
    if (!lex_accept(lex, TOK_LPAREN))
        return NULL;

    node = parse_compound(lex);
    if (!node) {
        syntax_error(lex, "Expected commands\n");
        return NULL;
    }

    if (!lex_accept(lex, TOK_RPAREN)) {
        syntax_error(lex, "Expected )\n");
        free_node(node);
        return NULL;
    }

    sub = alloc_node(CMD_SUBSHELL);
    sub->sub.background = 0;
    sub->sub.commands = node;
    return sub;
}

node_t *parse_brace(struct lexer *lex)
{
    node_t *node;
    if (!lex_accept(lex, TOK_LBRACE))
        return NULL;

    node = parse_compound(lex);
    if (!node) {
        syntax_error(lex, "Expected commands\n");
        return NULL;
    }

    if (!lex_accept(lex, TOK_RBRACE)) {
        syntax_error(lex, "Expected }\n");
        free_node(node);
        return NULL;
    }

    return node;
}

node_t *parse_single(struct lexer *lex)
{
    struct redirect *r, *rlist = NULL, **rptr = &rlist;
    enum tok tok = get_tok(lex);
    node_t *node = NULL;
    unget_tok(lex, tok);
    switch (tok) {
    case TOK_FOR:
        node = parse_for(lex);
        break;
    case TOK_CASE:
        node = parse_case(lex);
        break;
    case TOK_WHILE: case TOK_UNTIL:
        node = parse_loop(lex);
        break;
    case TOK_IF:
        node = parse_cond(lex);
        break;
    case TOK_LPAREN:
        node = parse_subshell(lex);
        break;
    case TOK_LBRACE:
        node = parse_brace(lex);
        break;
    default:
        node = NULL;
    }
    while ((r = parse_redirect(lex)))
        link_redirect(&rptr, r);
    return wrap_redirs(node, rlist);
}

int peek_special(struct lexer *lex)
{
    enum tok tok = get_tok(lex);
    unget_tok(lex, tok);
    switch (tok) {
    case TOK_DO: case TOK_DONE: case TOK_ELIF: case TOK_ELSE:
    case TOK_FI: case TOK_THEN: case TOK_RPAREN: case TOK_RBRACE:
        return 1;
    default:
        return 0;
    }
}

void andor_link(struct andor ***ptr, node_t *cmd, int negated, int and)
{
    struct andor *c = (void *)alloc_node(CMD_ANDOR);
    c->command = cmd;
    c->negated = negated;
    c->and = and;
    **ptr = c;
    *ptr = &c->next;
}

void pipeline_link(struct pipeline ***ptr, node_t *cmd)
{
    struct pipeline *c = (void *)alloc_node(CMD_PIPELINE);
    c->command = cmd;
    if (**ptr)
        (**ptr)->background = 1;
    **ptr = c;
    *ptr = &c->next;
}

node_t *make_background(node_t *node)
{
    node_t *sub;
    struct pipeline *last;
    switch (node->type) {
    case CMD_SUBSHELL:
        node->sub.background = 1;
        return node;
    case CMD_PIPELINE:
        node->pipe.background = 1;
        last = &node->pipe;
        while (last->next)
            last = last->next;
        assert(last->command->type != CMD_PIPELINE);
        last->command = make_background(last->command);
        return node;
    case CMD_SIMPLE:
        node->simp.background = 1;
        return node;
    default:
        break;
    }
    sub = alloc_node(CMD_SUBSHELL);
    sub->sub.background = 1;
    sub->sub.commands = node;
    return sub;
}

node_t *parse_andor(struct lexer *lex)
{
    int negated = 0, match = 0, is_and = 0;
    node_t *cmd = NULL;
    struct pipeline *plist = NULL, **pptr = &plist;
    struct andor *alist = NULL, **aptr = &alist;

    while (1) {
        negated = 0;
        while (lex_accept(lex, TOK_BANG)) {
            match = 1;
            negated = !negated;
        }

        plist = NULL;
        pptr = &plist;
        while (1) {
            if (!(cmd = parse_single(lex)) && !(cmd = parse_simple_command(lex))) {
                if (match && !lex->errored)
                    syntax_error(lex, "Expected a command\n");
                free_node((void *)plist);
                free_node((void *)alist);
                free_node(cmd);
                return NULL;
            }

            match = 1;

            if (!lex_accept(lex, TOK_PIPE))
                break;

            cmd = make_background(cmd);
            pipeline_link(&pptr, cmd);
            cmd = NULL;

            while (lex_accept(lex, TOK_NEWLINE));
        }

        if (plist) {
            if (cmd)
                pipeline_link(&pptr, cmd);
            cmd = (void *)plist;
        }

        if (!(is_and = lex_accept(lex, TOK_AND_IF)) && !lex_accept(lex, TOK_OR_IF))
            break;

        andor_link(&aptr, cmd, negated, is_and);
        cmd = NULL;

        while (lex_accept(lex, TOK_NEWLINE));
    }

    if (alist) {
        if (cmd)
            andor_link(&aptr, cmd, negated, 0);
        cmd = (void *)alist;
    }

    return cmd;
}

node_t *unwrap_compound(struct compound *c)
{
    node_t *ret;
    if (!c->next) {
        assert(c->base.refs == 1);
        ret = c->command;
        free(c);
        return ret;
    }
    return (void *)c;
}

void compound_link(struct compound ***ptr, node_t *cmd)
{
    struct compound *c = (void *)alloc_node(CMD_COMPOUND);
    c->command = cmd;
    **ptr = c;
    *ptr = &c->next;
}

node_t *parse_compound(struct lexer *lex)
{
    struct compound *clist = NULL, **cptr = &clist;
    node_t *node = NULL;

    while (lex_accept(lex, TOK_NEWLINE));

    while (1) {
        if (peek_special(lex))
            break;

        node = parse_andor(lex);
        if (!node)
            break;

        if (lex_accept(lex, TOK_AND)) {
            node = make_background(node);
            compound_link(&cptr, node);
        } else if (lex_accept(lex, TOK_SEMI) ||lex_accept(lex, TOK_NEWLINE)) {
            compound_link(&cptr, node);
        } else {
            free_node(node);
            free_node((void *)clist);
            syntax_error(lex, "Expected delimiter\n");
            return NULL;
        }

        while (lex_accept(lex, TOK_NEWLINE));
    }

    if (!clist) {
        syntax_error(lex, "Expected command\n");
        return NULL;
    }

    return unwrap_compound(clist);
}

node_t *parse(struct lexer *lex)
{
    struct compound *clist = NULL, **cptr = &clist;
    node_t *node = NULL;

    while (lex_accept(lex, TOK_NEWLINE));

    if (lex_accept(lex, TOK_EOF))
        return NULL;

    while((node = parse_andor(lex))) {
        if (lex_accept(lex, TOK_AND)) {
            node = make_background(node);
            compound_link(&cptr, node);
        } else if (lex_accept(lex, TOK_SEMI)) {
            compound_link(&cptr, node);
        } else if (lex_accept(lex, TOK_NEWLINE) || lex_accept(lex, TOK_EOF)) {
            compound_link(&cptr, node);
            break;
        } else {
            free_node(node);
            free_node((void *)clist);
            syntax_error(lex, "Expected & ; or LF\n");
            return NULL;
        }
    }

    if (!clist) {
        syntax_error(lex, "Expected commands\n");
        return NULL;
    }

    return unwrap_compound(clist);
}

struct shell_var {
    struct shell_var *next;
    int exported, read_only;
    str_t *name, *val;
};

enum eval_exit {
    EXIT_NEXT,
    EXIT_LOOP_CONTINUE,
    EXIT_LOOP_BREAK,
    EXIT_RETURN,
};

enum stack_type {
    STACK_LOOP,
};

struct stack_frame {
    struct stack_frame *prev;
    node_t *next_node, *loop_node;
    int argc;
    char **argv;
};
struct shell_func {
    struct shell_func *next;
    node_t *def;
};

struct args_frame {
    struct args_frame *prev;
    int argc, shift;
    char **argv;
};

struct shell {
    struct lexer lex;
    struct shell_var *vars;
    struct shell_func *funcs;
    struct args_frame *args;
    int exit_status;
    int in_func, break_depth, loop_depth;
};

void defun(struct shell *sh, struct function *def)
{
    struct shell_func **link, *func;
    for (link = &sh->funcs; (func = *link); link = &func->next) {
        if (str_eq(func->def->func.name, def->name)) {
            free_node(func->def);
            func->def = ref_node((void *)def);
            return;
        }
    }
    func = malloc(sizeof(*func));
    if (!func)
        abort();
    func->def = ref_node((void *)def);
    func->next = NULL;
    *link = func;
}

extern char **environ;

struct shell_var **getvarlink(struct shell *sh, const str_t *name)
{
    struct shell_var *var, **link;
    for (link = &sh->vars; (var = *link); link = &var->next)
        if (str_eq(var->name, name))
            return link;
    return link;
}

static void setvar(struct shell *sh, const str_t *name, const str_t *val, int exported)
{
    struct shell_var *var, **link;
    for (link = &sh->vars; (var = *link); link = &var->next) {
        if (str_eq(var->name, name)) {
            free_str(var->val);
            if (exported >= 0)
                var->exported = exported;
            var->val = dup_str(val);
            return;
        }
    }
    var = malloc(sizeof(*var));
    if (!var)
        abort();
    memset(var, 0, sizeof(*var));
    var->exported = (exported > 0);
    var->name = dup_str(name);
    var->val = dup_str(val);
    *link = var;
}

static const str_t *getvar(struct shell *sh, const str_t *name)
{
    struct shell_var **link = getvarlink(sh, name);
    if (!*link)
        return NULL;
    return (*link)->val;
}

static void shell_init(struct shell *sh)
{
    str_t name, val;
    char **env, *eq;
    memset(sh, 0, sizeof(*sh));
    for (env = environ; *env; env++) {
        eq = strchr(*env, '=');
        if (!eq)
            continue;
        name.start = name.buf_start = *env;
        name.end = name.buf_end = eq;
        val.start = val.buf_start = eq + 1;
        val.end = val.buf_end = eq + strlen(eq);
        setvar(sh, &name, &val, 1);
    }
    init_lex(&sh->lex, NULL);
}

static void destroy_shell(struct shell *sh)
{
    struct shell_var *v, *nv;
    struct shell_func *f, *nf;
    for (v = sh->vars; v; v = nv) {
        nv = v->next;
        free_str(v->name);
        free_str(v->val);
        free(v);
    }
    for (f = sh->funcs; f; f = nf) {
        nf = f->next;
        free_node(f->def);
        free(f);
    }
    destroy_lex(&sh->lex);
}

static int exists(const char *name)
{
    struct stat sb;
    return stat(name, &sb) == 0;
}

static char *slice(const char *str, size_t len)
{
    char *buf = malloc(len + 1), *ptr;
    if (!buf)
        return NULL;
    strncpy(buf, str, len);
    buf[len] = 0;
    ptr = realloc(buf, strlen(buf) + 1);
    return !ptr ? buf : ptr;
}

static char *find_on_path(struct shell *sh, const char *cmd)
{
    const str_t *var;
    str_t tmp;
    const char *path, *end;
    char *name, *prefix;
    size_t prefix_len;

    tmp.start = tmp.buf_start = "PATH";
    tmp.end = tmp.buf_end = tmp.start + strlen((void *)tmp.start);
    var = getvar(sh, &tmp);
    if (strchr(cmd, '/') || str_empty(var))
        return strdup(cmd);
    path = (const char *)var->start;
    while (path) {
        end = strchr(path, ':');
        if (!end) {
            prefix_len = strlen(path);
        } else {
            prefix_len = end - path;
        }

        if (!prefix_len) {
            prefix = strdup(".");
        } else {
            prefix = slice(path, prefix_len);
        }

        if (!prefix)
            return NULL;

        name = malloc(strlen(prefix) + strlen(cmd) + 2);
        if (!name)
            abort();
        *name = 0;
        strcat(name, prefix);
        strcat(name, "/");
        strcat(name, cmd);

        free(prefix);

        if (name && exists(name))
            return name;

        free(name);
        path = end ? end + 1 : NULL;
    }
    return NULL;
}

const char *strop(enum tok tok)
{
    const struct tok_def *def;
    for (def = ops; def; def++)
        if (def->type == tok)
            return def->tok;
    return "?";
}

void show_redirs(struct redirect *redirs) {
    struct redirect *r;
    for (r = redirs; r; r = r->next) {
        if (r->fd >= 0)
            printf("%d", r->fd);
        printf("%s", strop(r->op));
        if (r->doc) {
            printf("HEREDOC ");
        } else {
            printf("%s ", r->name->tok->start);
        }
    }
}

void debug_show_node(node_t *node)
{
    struct var *v;
    struct arg *a;
    struct item *i;

    if (!node) {
        printf("(null)\n");
        return;
    }

    switch (node->type) {
    case CMD_SIMPLE: case CMD_ASSIGNMENT:
        for (v = node->simp.vars; v; v = v->next)
            printf("%s='%s' ", v->name->start, v->val->tok->start);
        for (a = node->simp.args; a; a = a->next)
            printf("%s ", a->val->tok->start);
        show_redirs(node->simp.redirs);
        break;
    case CMD_SUBSHELL:
        printf("( ");
        debug_show_node(node->sub.commands);
        printf(node->sub.background ? ")&" : ")");
        break;
    case CMD_COMPOUND:
        printf("{ ");
        for (; node; node = (void *)node->comp.next) {
            debug_show_node(node->comp.command);
            printf(";");
        }
        printf(" }");
        break;
    case CMD_PIPELINE:
        for (; node; node = (void *)node->pipe.next) {
            debug_show_node(node->pipe.command);
            printf("|");
        }
        break;
    case CMD_LOOP:
        printf(node->loop.until ? "until " : "while ");
        debug_show_node(node->loop.cond);
        printf("; do ");
        debug_show_node(node->loop.commands);
        printf("; done");
        break;
    case CMD_ANDOR:
        for (; node; node = (void *)node->andor.next) {
            if (node->andor.negated)
                printf("! ");
            debug_show_node(node->andor.command);
            if (node->andor.next)
                printf(node->andor.and ? " && " : " || ");
        }
        break;
    case CMD_COND:
        while (1) {
            printf("if ");
            debug_show_node(node->cond.cond);
            printf("; then ");
            debug_show_node(node->cond.commands);
            if (!node->cond.otherwise)
                break;
            if (node->cond.otherwise->type != CMD_COND) {
                printf("; else ");
                debug_show_node(node->cond.otherwise);
                break;
            }
            node = node->cond.otherwise;
            printf("; el");
        }
        printf("; endif");
        break;
    case CMD_REDIRS:
        debug_show_node(node->redirs.command);
        show_redirs(node->redirs.redirs);
        break;
    case CMD_FUNCTION:
        printf("%s ( ) { ", node->func.name->start);
        debug_show_node(node->func.command);
        printf("; }");
        break;
    case CMD_FOR_LOOP:
        printf("for %s ", node->for_loop.name->start);
        if (!node->for_loop.use_args) {
            printf("in ");
            for (i = node->for_loop.items; i; i = i->next)
                printf("%s ", i->val->tok->start);
        }
        printf("; do ");
        debug_show_node(node->for_loop.command);
        printf("; done");
        break;
    case CMD_CASES:
        //TODO
        break;
    }
}

char **make_env(struct shell *sh, struct cmd *cmd)
{
    struct shell_var *svar;
    struct var *var;
    struct {str_t *name; str_t *val;} *real_vars;
    size_t count = 0, size = 0, i = 0, len;
    char **vars, **vend, *end;
    for (svar = sh->vars; svar; svar = svar->next)
        count++;
    for (var = cmd->vars; var; var = var->next)
        count++;
    if (count == SIZE_MAX)
        abort();
    real_vars = calloc(count, sizeof(*real_vars));
    if (!real_vars)
        abort();
    count = 0;
    for (svar = sh->vars; svar; svar = svar->next) {
        if (!svar->exported)
            continue;
        for (i = 0; i < count; i++) {
            if (str_eq(real_vars[i].name, svar->name)) {
                real_vars[i].val = svar->val;
                break;
            }
        }
        if (i == count) {
            real_vars[i].name = svar->name;
            real_vars[i].val = svar->val;
            count++;
        }
    }
    for (var = cmd->vars; var; var = var->next) {
        for (i = 0; i < count; i++) {
            if (str_eq(real_vars[i].name, var->name)) {
                real_vars[i].val = var->val->tok;
                break;
            }
        }
        if (i == count) {
            real_vars[i].name = var->name;
            real_vars[i].val = var->val->tok;
            count++;
        }
    }
    size = sizeof(char *);
    for (i = 0; i < count; i++) {
        len = str_len(real_vars[i].name);
        if (size > SIZE_MAX - len)
            abort();
        size += len;
        len = str_len(real_vars[i].val);
        if (size > SIZE_MAX - len)
            abort();
        size += len;
        if (size > SIZE_MAX - 2)
            abort();
        size += 2;
        if (size > SIZE_MAX - sizeof(char *))
            abort();
        size += sizeof(char *);
    }
    vars = malloc(size);
    if (!vars)
        abort();
    vend = vars;
    end = (char *)(vars + count + 1);
    for (i = 0; i < count; i++) {
        *vend++ = end;
        len = str_len(real_vars[i].name);
        memcpy(end, real_vars[i].name->start, len);
        end += len;
        *end++ = '=';
        len = str_len(real_vars[i].val);
        memcpy(end, real_vars[i].val->start, len);
        end += len;
        *end++ = 0;
    }
    *vend++ = 0;
    free(real_vars);
    return vars;
}

struct split {
    struct split *next;
    char *str;
};

void put_split(struct split ***splits, char *str)
{
    struct split *split = malloc(sizeof(*split));
    if (!split)
        abort();
    split->str = str;
    split->next = NULL;
    **splits = split;
    *splits = &split->next;
}

char **join_splits(struct split *slist)
{
    char **splits, **vend, *end;
    struct split *s, *sn;
    size_t count = 1, size = sizeof(char *), len;

    for (s = slist; s; s = s->next) {
        len = strlen(s->str) + 1;
        if (len > SIZE_MAX - size)
            abort();
        size += len;
        len = sizeof(char *);
        if (len > SIZE_MAX - size)
            abort();
        size += len;
        count++;
    }

    splits = malloc(size);
    if (!splits)
        abort();
    vend = splits;
    end = (void*)(vend + count);

    for (s = slist; s; s = sn) {
        len = strlen(s->str);
        strcpy(end, s->str);
        *vend++ = end;
        end += len + 1;
        sn = s->next;
        free(s->str);
        free(s);
    }

    *vend++ = 0;

    return splits;
}

size_t split_ifs(struct split ***sptr, str_t *buf, const char *ifs, size_t start)
{
    char *str;
    size_t len = str_len(buf);
    if (!ifs)
        return len;
    while (start < len) {
        if (strchr(ifs, buf->start[start])) {
            str = malloc(start + 1);
            if (!str)
                abort();
            memcpy(str, buf->start, start);
            str[start] = 0;
            buf->start += start + 1;
            len -= start + 1;
            start = 0;
            put_split(sptr, str);
        } else {
            start++;
        }
    }
    return start;
}

void expand_into(str_t *buf, struct shell *sh, word_t *word)
{
    const str_t *tmp;
    switch (word->type) {
    case WORD_PARAMETER:
        tmp = getvar(sh, word->tok);
        if (tmp)
            str_put(buf, tmp->start, str_len(tmp));
        break;
    case WORD_STRING:
        str_put(buf, word->tok->start, str_len(word->tok));
        break;
    default:
        abort();
    }
}

void expand(struct split ***sptr, struct shell *sh, word_t *word)
{
    char name_s[3] = "IFS";
    str_t name = {(void *)name_s, (void *)(name_s + sizeof(name_s)), name_s, name_s + sizeof(name_s)};
    const str_t *tmp = getvar(sh, &name);
    char *ifs = tmp ? strdup((void *)tmp->start) : strdup(" \t");
    char *rest;
    str_t *buf = new_str();
    size_t next_split = 0;
    for (; word; word = word->next) {
        expand_into(buf, sh, word);
        if (word->quoted) {
            next_split = str_len(buf);
        } else {
            next_split = split_ifs(sptr, buf, ifs, next_split);
        }
    }
    rest = strdup((void *)buf->start);
    if (!rest)
        abort();
    put_split(sptr, rest);
    free(ifs);
    free_str(buf);
}

char **expand_join(struct shell *sh, word_t *word)
{
    struct split *slist = NULL, **sptr = &slist;
    expand(&sptr, sh, word);
    return join_splits(slist);
}

char **make_args(struct shell *sh, struct cmd *cmd)
{
    struct split *slist = NULL, **sptr = &slist;
    struct arg *arg;
    for (arg = cmd->args; arg; arg = arg->next)
        expand(&sptr, sh, arg->val);
    return join_splits(slist);
}

static void do_assign(struct shell *sh, struct cmd *cmd)
{
    word_t *w;
    struct var *v;
    str_t *buf = new_str();
    for (v = cmd->vars; v; v = v->next) {
        str_clear(buf);
        for (w = v->val; w; w = w->next)
            expand_into(buf, sh, w);
        setvar(sh, v->name, buf, -1);
    }
    free_str(buf);
}

void enter_subshell(struct shell *sh)
{
    sh->loop_depth = 0;
    sh->in_func = 0;
}

struct savedfd {
    struct savedfd *next;
    int old_fd, new_fd;
};

void revert_redirs(struct shell *sh, struct savedfd *save)
{
    struct savedfd *next;
    (void) sh;
    while (save) {
        next = save->next;
        if (save->old_fd >= 0)
            dup2(save->new_fd, save->old_fd);
        close(save->new_fd);
        free(save);
        save = next;
    }
}

struct savedfd *apply_redirs(struct shell *sh, struct redirect *redirs)
{
    struct savedfd *save = NULL, **sptr = &save, *r;
    struct redirect *next_redir;
    char **names;
    int err;
    int fd, saved_fd;
    long fd2;
    for (next_redir = redirs; (redirs = next_redir); next_redir = redirs->next) {
        if (!redirs->name)
            continue;
        names = expand_join(sh, redirs->name);
        if (!names)
            abort();
        errno = 0;
        err = 0;
        fd2 = strtol(names[0], NULL, 10);
        if (errno || fd2 < 0 || fd2 > INT_MAX)
            err = 1;
        switch (redirs->op) {
        default:
            errno = EINVAL;
            fd = -1;
            break;
        case TOK_LESS:
            fd = open(names[0], O_RDONLY);
            break;
        case TOK_LESSGREAT:
            fd = open(names[0], O_RDWR | O_CREAT, 0777);
            break;
        case TOK_GREAT:
        case TOK_CLOBBER:
            fd = open(names[0], O_WRONLY | O_CREAT, 0777);
            break;
        case TOK_DGREAT:
            fd = open(names[0], O_WRONLY | O_CREAT | O_APPEND, 0777);
            break;
        case TOK_LESSAND:
        case TOK_GREATAND:
            if (!err) {
                fd = dup(fd2);
            } else {
                errno = EINVAL;
                fd = -1;
            }
            if (strcmp(names[0], "-"))
                fd = -2;
            break;
        }
        free(names);
        if (fd == -1) {
            perror("failed to open file");
            goto fail;
        }

        saved_fd = dup(redirs->fd);
        if (saved_fd < 0) {
            close(fd);
            goto fail;
        }

        close(redirs->fd);
        if (fd >= 0) {
            if (dup2(fd, redirs->fd) < 0) {
                close(fd);
                goto fail;
            }
        }

        r = malloc(sizeof(*r));
        if (!r)
            abort();
        r->next = NULL;
        r->old_fd = redirs->fd;
        r->new_fd = saved_fd;
        *sptr = r;
        sptr = &r->next;
    }
    return save;

fail:
    revert_redirs(sh, save);
    return NULL;
}

void exec_simple(struct shell *sh, struct cmd *cmd)
{
    char *path = NULL, **args = NULL, **env = NULL;
    apply_redirs(sh, cmd->redirs);
    args = make_args(sh, cmd);
    if (!args)
        _exit(1);
    path = find_on_path(sh, args[0]);
    if (!path)
        _exit(127);
    env = make_env(sh, cmd);
    if (!env)
        _exit(1);
    execve(path, args, env);
    if (errno == ENOENT)
        _exit(127);
}

typedef int (*builtin_t)(struct shell *sh, int argc, char **argv);

struct builtin {
    const char *name;
    builtin_t func;
};

enum eval_exit do_eval(struct shell *sh, node_t *node);

void wait_job(struct shell *sh, pid_t pgid, pid_t pid, int background)
{
    siginfo_t info;
    if (background)
        return;
    if (pgid < 0)
        pgid = pid;
    if (pgid < 0 && pid < 0) {
        return;
    }
    while (1) {
        if (waitid(P_PGID, pgid, &info, WNOHANG | WEXITED) < 0) {
            if (errno == ECHILD)
                break;
            continue;
        }
        if (info.si_pid == pid)
            sh->exit_status = info.si_status & 0xff;
    }
}

enum eval_exit eval_simple(struct shell *sh, struct cmd *cmd)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        enter_subshell(sh);
        exec_simple(sh, cmd);
        _exit(1);
    } else if (pid < 0) {
        sh->exit_status = 1;
        return EXIT_NEXT;
    } else {
        setpgid(pid, pid);
        wait_job(sh, pid, pid, cmd->background);
        return EXIT_NEXT;
    }
}

enum eval_exit eval_subshell(struct shell *sh, struct subshell *sub)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        enter_subshell(sh);
        do_eval(sh, sub->commands);
        _exit(sh->exit_status);
    } else if (pid < 0) {
        sh->exit_status = 1;
    } else {
        setpgid(pid, pid);
        wait_job(sh, pid, pid, sub->background);
    }
    return EXIT_NEXT;
}

enum eval_exit eval_pipeline(struct shell *sh, struct pipeline *pipes)
{
    pid_t pgid = -1, pid = -1;
    int fd[2], input = STDIN_FILENO, output, next_input;
    int background = 0;
    if (input < 0)
        goto error;
    while (pipes) {
        if (pipes->next) {
            if (pipe(fd) < 0) {
                pid = -1;
                goto error;
            }
            output = fd[1];
            next_input = fd[0];
        } else {
            output = STDOUT_FILENO;
            next_input = -1;
        }

        pid = fork();
        if (pid == 0) {
            if (next_input > 0)
                close(next_input);
            if (input != STDIN_FILENO) {
                if (dup2(input, STDIN_FILENO) < 0) {
                    perror("dupa");
                    _exit(1);
                }
                close(input);
            }
            if (output != STDOUT_FILENO) {
                if (dup2(output, STDOUT_FILENO) < 0) {
                    perror("dupb");
                    _exit(1);
                }
                close(output);
            }
            setpgid(0, pgid < 0 ? 0 : pgid);
            enter_subshell(sh);
            if (pipes->command->type == CMD_SIMPLE)
                exec_simple(sh, &pipes->command->simp);
            do_eval(sh, pipes->command);
            _exit(sh->exit_status);
        } else if(pid > 0) {
            if (pgid < 0)
                pgid = pid;
            setpgid(pid, pgid);
            if (input != STDIN_FILENO)
                close(input);
            if (output != STDOUT_FILENO)
                close(output);
            input = next_input;
            background = pipes->background;
            pipes = pipes->next;
        } else {
            goto error;
        }
    }
    wait_job(sh, pgid, pid, background);
    return EXIT_NEXT;
error:
    wait_job(sh, pgid, pid, background);
    sh->exit_status = 1;
    return EXIT_NEXT;
}

enum eval_exit eval_redirs(struct shell *sh, struct redirs *redirs)
{
    enum eval_exit ret;
    struct savedfd *save = apply_redirs(sh, redirs->redirs);
    if (!save) {
        sh->exit_status = 1;
        return EXIT_NEXT;
    }
    ret = do_eval(sh, redirs->command);
    revert_redirs(sh, save);
    return ret;
}

enum eval_exit eval_andor(struct shell *sh, struct andor *andor)
{
    enum eval_exit ret;
    int should_eval = 1;
    while (andor) {
        if (should_eval) {
            ret = do_eval(sh, andor->command);
            if (ret != EXIT_NEXT)
                return ret;
            if (andor->negated)
                sh->exit_status = !sh->exit_status;
        }
        if (andor->and)
            should_eval = !sh->exit_status;
        else
            should_eval = sh->exit_status;
        andor = andor->next;
    }
    return EXIT_NEXT;
}

enum eval_exit eval_compound(struct shell *sh, struct compound *comp)
{
    enum eval_exit ret;
    while (comp) {
        ret = do_eval(sh, comp->command);
        if (ret != EXIT_NEXT)
            return ret;
        comp = comp->next;
    }
    return EXIT_NEXT;
}

enum eval_exit eval_cond(struct shell *sh, struct cond *cond)
{
    enum eval_exit ret;
    while (1) {
        ret = do_eval(sh, cond->cond);
        if (ret != EXIT_NEXT)
            return ret;
        if (!sh->exit_status) {
            return do_eval(sh, cond->commands);
        } else {
            if (cond->otherwise->type == CMD_COND) {
                cond = (void *)cond->otherwise;
                continue;
            }
            return do_eval(sh, cond->otherwise);
        }
    }
}

enum eval_exit eval_loop(struct shell *sh, struct loop *loop)
{
    enum eval_exit ret;
    sh->loop_depth++;
    while (1) {
        ret = do_eval(sh, loop->cond);
        if (ret != EXIT_NEXT)
            goto loop_exit;
        if ((loop->until && !sh->exit_status) || (!loop->until && sh->exit_status)) {
            ret = EXIT_NEXT;
            goto loop_exit;
        }
        ret = do_eval(sh, loop->commands);
        switch (ret) {
        case EXIT_LOOP_CONTINUE:
            if (!--sh->break_depth)
                break;
            goto loop_exit;
        case EXIT_LOOP_BREAK:
            if (!--sh->break_depth)
                ret = EXIT_NEXT;
            goto loop_exit;
        case EXIT_RETURN:
            goto loop_exit;
        case EXIT_NEXT:
            break;
        }
    }
loop_exit:
    sh->loop_depth--;
    return ret;
}

enum eval_exit do_eval(struct shell *sh, node_t *node)
{
    switch (node->type) {
    case CMD_ASSIGNMENT:
        do_assign(sh, &node->simp);
        return EXIT_NEXT;
    case CMD_SIMPLE:
        return eval_simple(sh, &node->simp);
    case CMD_ANDOR:
        return eval_andor(sh, &node->andor);
    case CMD_PIPELINE:
        return eval_pipeline(sh, &node->pipe);
    case CMD_COMPOUND:
        return eval_compound(sh, &node->comp);
    case CMD_SUBSHELL:
        return eval_subshell(sh, &node->sub);
    case CMD_LOOP:
        return eval_loop(sh, &node->loop);
    case CMD_COND:
        return eval_cond(sh, &node->cond);
    case CMD_REDIRS:
        return eval_redirs(sh, &node->redirs);
    case CMD_FOR_LOOP:
        //TODO
        return EXIT_NEXT;
    case CMD_FUNCTION:
        defun(sh, &node->func);
        return EXIT_NEXT;
    case CMD_CASES:
        //TODO
        return EXIT_NEXT;
    }
    abort();
}

void run_shell(struct shell *sh, node_t *root)
{
    enum eval_exit ret;
    assert(!sh->break_depth && !sh->in_func && !sh->loop_depth);
    ret = do_eval(sh, root);
    assert(!sh->break_depth && !sh->in_func && !sh->loop_depth);
    switch (ret) {
    case EXIT_NEXT:
        free_node(root);
        return;
    default:
        abort();
    }
}

int main()
{
    node_t *cmd;
    struct shell sh;
    setpgid(0, 0);
    shell_init(&sh);
    while (!sh.lex.errored) {
        cmd = parse(&sh.lex);
        if (!cmd)
            break;
        run_shell(&sh, cmd);
    }
    destroy_shell(&sh);
}

