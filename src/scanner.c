#include "scanner.h"

#include <stdio.h>
#include <string.h>

#include "common.h"

Scanner scanner;

Scanner initScanner(const char *source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;

  return scanner;
}

void printScanner(Scanner sc) {
  fprintf(stderr, "(scanner current): %s\n", scanner.current);
}

Scanner saveScanner() {
  Scanner checkpoint = scanner;
  return checkpoint;
}

void gotoScanner(Scanner checkpoint) { scanner = checkpoint; }

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isSymbol(char c) {
  return (c == '&' || c == '^' || c == '@' || c == '#' || c == '~' ||
          c == '?' || c == '$' || c == '\'');
}

static bool isAtEnd() { return *scanner.current == '\0'; }

static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

static char peek() { return *scanner.current; }

char charAt(int i) { return scanner.current[i]; }

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token errorToken(const char *message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

bool isWhite(char c) { return c == ' ' || c == '\r' || c == '\t' || c == '\n'; }

void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        scanner.line++;
        advance();
        break;
      case '/':
        if (peekNext() == '/') {
          // A comment goes until the end of the line.
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static TokenType checkpointKeyword(int start, int length, const char *rest,
                                   TokenType type) {
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
  switch (scanner.start[0]) {
    case 'a':
      return checkpointKeyword(1, 2, "nd", TOKEN_AND);
    case 'c':
      return checkpointKeyword(1, 4, "lass", TOKEN_CLASS);
    case 'e':
      return checkpointKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a':
            return checkpointKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o':
            return checkpointKeyword(2, 1, "r", TOKEN_FOR);
        }
      }
      break;
    case 'i':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'f':
            return checkpointKeyword(1, 1, "f", TOKEN_IF);
          case 'm':
            return checkpointKeyword(2, 4, "port", TOKEN_IMPORT);
          case 'n': {
            if (scanner.current - scanner.start > 2) {
              switch (scanner.start[2]) {
                case 'f':
                  return checkpointKeyword(3, 2, "ix", TOKEN_INFIX);
              }
            }
            return checkpointKeyword(1, 1, "n", TOKEN_IN);
          }
        }
      }
      break;
    case 'l':
      return checkpointKeyword(1, 2, "et", TOKEN_LET);
    case 'n':
      return checkpointKeyword(1, 2, "il", TOKEN_NIL);
    case 'o':
      return checkpointKeyword(1, 1, "r", TOKEN_OR);
    case 'p':
      return checkpointKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
      return checkpointKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
      return checkpointKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'h': {
            if (scanner.current - scanner.start > 2) {
              switch (scanner.start[2]) {
                case 'i':
                  return checkpointKeyword(2, 2, "is", TOKEN_THIS);
                case 'r':
                  return checkpointKeyword(2, 3, "row", TOKEN_THROW);
              }
            }
            break;
          }
          case 'r':
            return checkpointKeyword(2, 2, "ue", TOKEN_TRUE);
        }
      }
      break;
    case 'w':
      return checkpointKeyword(1, 4, "hile", TOKEN_WHILE);
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while (isAlpha(peek()) || isDigit(peek()) || isSymbol(peek())) advance();
  return makeToken(identifierType());
}

Token dottedIdentifier() {
  while (isAlpha(peek()) || isDigit(peek()) || peek() == '/') advance();
  return scanner.current == scanner.start ? errorToken("Unexpected character.")
                                          : makeToken(TOKEN_IDENTIFIER);
}

static Token number() {
  while (isDigit(peek())) advance();

  // Look for a fractional part.
  if (peek() == '.' && isDigit(peekNext())) {
    // Consume the ".".
    advance();

    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  // The closing quote.
  advance();
  return makeToken(TOKEN_STRING);
}

Token scanToken() {
  skipWhitespace();
  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c) || isSymbol(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(':
      return makeToken(TOKEN_LEFT_PAREN);
    case ')':
      return makeToken(TOKEN_RIGHT_PAREN);
    case '{':
      return makeToken(TOKEN_LEFT_BRACE);
    case '}':
      return makeToken(TOKEN_RIGHT_BRACE);
    case '[':
      return makeToken(TOKEN_LEFT_BRACKET);
    case ']':
      return makeToken(TOKEN_RIGHT_BRACKET);
    case ':':
      return makeToken(TOKEN_COLON);
    case ';':
      return makeToken(TOKEN_SEMICOLON);
    case ',':
      return makeToken(TOKEN_COMMA);
    case '.':
      return makeToken(TOKEN_DOT);
    case '-':
      return makeToken(TOKEN_MINUS);
    case '+':
      return makeToken(TOKEN_PLUS);
    case '/':
      return makeToken(TOKEN_SLASH);
    case '*':
      return makeToken(TOKEN_STAR);
    case '|':
      return makeToken(TOKEN_PIPE);
    case '!':
      return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      if (match('>')) {
        return makeToken(TOKEN_ARROW);
      } else {
        return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
      }
    case '<':
      return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
      return string();
  }

  return errorToken("Unexpected character.");
}