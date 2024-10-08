#ifndef nat_scanner_h
#define nat_scanner_h

#include "common.h"

typedef enum {
  // Single-character tokens.
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_DOUBLE_DOT,
  TOKEN_COLON,
  TOKEN_SEMICOLON,
  TOKEN_PIPE,
  // One or two character tokens.
  TOKEN_FAT_ARROW,
  TOKEN_ARROW_LEFT,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  // Literals.
  TOKEN_UNDEFINED,
  TOKEN_IDENTIFIER,
  TOKEN_TYPE_VARIABLE,
  TOKEN_STRING,
  TOKEN_INTERPOLATION,
  TOKEN_NUMBER,
  // Keywords.
  TOKEN_AND,
  TOKEN_AS,
  TOKEN_CLASS,
  TOKEN_CONST,
  TOKEN_DOM,
  TOKEN_ELSE,
  TOKEN_EXTENDS,
  TOKEN_FALSE,
  TOKEN_FOR,
  TOKEN_IF,
  TOKEN_IN,
  TOKEN_INFIX,
  TOKEN_INFIX_LEFT,
  TOKEN_INFIX_RIGHT,
  TOKEN_LET,
  TOKEN_NIL,
  TOKEN_NOT,
  TOKEN_OR,
  TOKEN_PRINT,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_THROW,
  TOKEN_TRUE,
  TOKEN_WHILE,
  TOKEN_IMPORT,
  TOKEN_ERROR,
  TOKEN_EOF,
} TokenType;

typedef struct {
  TokenType type;
  const char* start;
  int length;
  int line;
} Token;

typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

Scanner initScanner(const char* source);
Scanner saveScanner();
void initToken(Token* token);
Token syntheticToken(const char* start);
Token virtualToken(char c);
void gotoScanner(Scanner scanner);
void rewindScanner(Token token);
void printScanner(Scanner sc);
void skipWhitespace();
Token scanToken();
Token slashedIdentifier();
char charAt(int i);
bool isWhite(char c);

#endif