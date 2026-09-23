#ifndef DOOM_FS_CTYPE_H
#define DOOM_FS_CTYPE_H
int toupper(int c);
int tolower(int c);
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isalpha(int c) { return (c|32) >= 'a' && (c|32) <= 'z'; }
static inline int isspace(int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
#endif
