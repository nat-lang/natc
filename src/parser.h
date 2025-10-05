#ifndef nat_parser_h
#define nat_parser_h

#include "common.h"
#include "scanner.h"

typedef struct {
  Scanner scanner;

  Token next;
  Token current;
  Token previous;
  Token penult;

  bool hadError;
  bool panicMode;
} Parser;

typedef enum { SIG_NAKED, SIG_PAREN, SIG_NOT } SignatureType;

extern Parser parser;

void initParser(Scanner scanner);
Parser saveParser();
void gotoParser(Parser checkpoint);

bool prev(TokenType type);
bool check(TokenType type);
bool peek(TokenType type);
bool checkVariable();
void errorAt(Token* token, const char* message);
void errorAtCurrent(const char* message);
void error(const char* message);
void checkError();
bool check(TokenType type);
void advance();
bool match(TokenType type);
void consume(TokenType type, const char* message);
void consumeIdentifier(const char* message);
ObjString* parseVariable(const char* errorMessage);
bool matchParamOrPattern();
SignatureType peekSignatureType();

#endif