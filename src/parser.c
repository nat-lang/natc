#include "parser.h"

#include <stdio.h>

#include "object.h"
Parser parser;

void initParser(Scanner scanner) {
  parser.scanner = scanner;

  parser.hadError = false;
  parser.panicMode = false;
  parser.current = scanToken();
  parser.next = scanToken();
}

Parser saveParser() {
  Parser checkpoint = parser;

  checkpoint.scanner = saveScanner();

  return checkpoint;
}

void gotoParser(Parser checkpoint) {
  gotoScanner(checkpoint.scanner);
  parser = checkpoint;
}

bool prev(TokenType type) { return parser.previous.type == type; }
bool check(TokenType type) { return parser.current.type == type; }
bool peek(TokenType type) { return parser.next.type == type; }

bool checkVariable() {
  return check(TOKEN_IDENTIFIER) || check(TOKEN_TYPE_VARIABLE);
}

void errorAt(Token* token, const char* message) {
  if (parser.panicMode)
    return;
  else
    parser.panicMode = true;

  fprintf(stderr, "%s", message);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

void errorAtCurrent(const char* message) { errorAt(&parser.current, message); }

void error(const char* message) { errorAt(&parser.previous, message); }

void checkError() {
  if (parser.current.type == TOKEN_ERROR) errorAtCurrent(parser.current.start);
}

void shiftParser() {
  parser.penult = parser.previous;
  parser.previous = parser.current;
  parser.current = parser.next;
}

void advance() {
  shiftParser();
  parser.next = scanToken();
  checkError();
}

bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

void consume(TokenType type, const char* message) {
  if (parser.current.type == type)
    advance();
  else
    errorAtCurrent(message);
}

void consumeIdentifier(const char* message) {
  if (parser.current.type == TOKEN_IDENTIFIER ||
      parser.current.type == TOKEN_TYPE_VARIABLE)
    advance();
  else
    errorAtCurrent(message);
}

ObjString* parseVariable(const char* errorMessage) {
  consumeIdentifier(errorMessage);
  Token token = parser.previous;
  return copyString(token.start, token.length);
}

bool matchParamOrPattern() {
  return match(TOKEN_IDENTIFIER) || match(TOKEN_TYPE_VARIABLE) ||
         match(TOKEN_NUMBER) || match(TOKEN_TRUE) || match(TOKEN_FALSE) ||
         match(TOKEN_NIL) || match(TOKEN_UNDEFINED) || match(TOKEN_STRING);
}

static bool advanceTo(TokenType token, TokenType closing, int initialDepth) {
  int depth = initialDepth;

  for (;;) {
    if (check(TOKEN_LEFT_BRACE) || check(TOKEN_LEFT_BRACKET) ||
        check(TOKEN_PAREN_LEFT))
      depth++;
    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_RIGHT_BRACKET) ||
        check(TOKEN_PAREN_RIGHT))
      depth--;
    // found one.
    if (check(token) && depth == initialDepth) return true;
    // found none.
    if (check(closing) && depth == 0) return false;
    advance();
  }
}

SignatureType peekSignatureType() {
  if (match(TOKEN_PAREN_LEFT)) {
    if (!check(TOKEN_PAREN_RIGHT)) {
      do {
        if (!matchParamOrPattern()) return SIG_NOT;
        if (check(TOKEN_COLON)) {
          advanceTo(TOKEN_COMMA, TOKEN_PAREN_RIGHT, 1);
        }

      } while (match(TOKEN_COMMA));
    }

    if (!match(TOKEN_PAREN_RIGHT)) return SIG_NOT;
    if (!match(TOKEN_FAT_ARROW)) return SIG_NOT;

    return SIG_PAREN;
  } else {
    // can only be naked.
    if (matchParamOrPattern()) return peekSignatureType();
    if (match(TOKEN_FAT_ARROW)) return SIG_NAKED;
  }

  return SIG_NOT;
}