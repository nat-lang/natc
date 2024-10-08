#include "scanner.h"

#include <stdio.h>
#include <string.h>

Scanner scanner;

Scanner initScanner(const char *source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;

  return scanner;
}

void initToken(Token *token) {
  token->type = -1;
  token->start = NULL;
  token->length = -1;
  token->line = -1;
}

Token syntheticToken(const char *start) {
  Token token;
  token.start = start;
  token.length = (int)strlen(start);
  return token;
}

void printScanner(Scanner sc) {
  fprintf(stderr, "(scanner current): %s\n", scanner.current);
}

Scanner saveScanner() {
  Scanner checkpoint = scanner;
  return checkpoint;
}

void gotoScanner(Scanner checkpoint) { scanner = checkpoint; }

void rewindScanner(Token token) {
  scanner.line = token.line;
  scanner.current = scanner.start = token.start;
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isTypeAlpha(char c) { return (c >= 'u' && c <= 'z'); }

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

static bool isSymbol(char c) {
  return (c == '&' || c == '^' || c == '@' || c == '#' || c == '~' ||
          c == '?' || c == '$' || c == '\'' || c == '>' || c == '<' ||
          c == '+' || c == '-' || c == '/' || c == '*' || c == '|' ||
          c == '=' || c == '_' || c == '%');
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

static char prev() { return scanner.current[-1]; }

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
          // a comment goes until the end of the line.
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
#define START scanner.start
#define CURRENT scanner.current

  switch (START[0]) {
    case 'a':
      return checkpointKeyword(1, 1, "s", TOKEN_AS);
    case 'c': {
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'o':
            return checkpointKeyword(2, 3, "nst", TOKEN_CONST);
          case 'l':
            return checkpointKeyword(2, 3, "ass", TOKEN_CLASS);
        }
      }
      break;
    }
    case 'd':
      return checkpointKeyword(1, 2, "om", TOKEN_DOM);
    case 'e':
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'l':
            return checkpointKeyword(2, 2, "se", TOKEN_ELSE);
          case 'x':
            return checkpointKeyword(2, 5, "tends", TOKEN_EXTENDS);
        }
      }
      break;
    case 'f':
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'a':
            return checkpointKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o':
            return checkpointKeyword(2, 1, "r", TOKEN_FOR);
        }
      }
      break;
    case 'i':
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'f':
            return checkpointKeyword(1, 1, "f", TOKEN_IF);
          case 'm':
            return checkpointKeyword(2, 4, "port", TOKEN_IMPORT);
          case 'n': {
            if (CURRENT - START > 2) {
              switch (START[2]) {
                case 'f': {
                  if (CURRENT - START > 3) {
                    switch (START[3]) {
                      case 'i': {
                        if (CURRENT - START > 4) {
                          switch (START[4]) {
                            case 'x': {
                              if (CURRENT - START > 5) {
                                switch (START[5]) {
                                  case 'l':
                                    return TOKEN_INFIX_LEFT;
                                  case 'r':
                                    return TOKEN_INFIX_RIGHT;
                                }
                              }
                              return TOKEN_INFIX;
                            }
                          }
                        }
                      }
                    }
                  }
                }
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
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'i':
            return checkpointKeyword(2, 1, "l", TOKEN_NIL);
          case 'o':
            return checkpointKeyword(2, 1, "t", TOKEN_NOT);
        }
      }
      break;
    case 'p':
      return checkpointKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
      return checkpointKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
      return checkpointKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (CURRENT - START > 1) {
        switch (START[1]) {
          case 'h': {
            if (CURRENT - START > 2) {
              switch (START[2]) {
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
    case 'u':
      return checkpointKeyword(1, 8, "ndefined", TOKEN_UNDEFINED);
    case 'w':
      return checkpointKeyword(1, 4, "hile", TOKEN_WHILE);
    case '&':
      return checkpointKeyword(1, 1, "&", TOKEN_AND);
    case '|': {
      return checkpointKeyword(1, 1, "|", TOKEN_OR);
    }
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while (isAlpha(peek()) || isDigit(peek()) || isSymbol(peek())) advance();
  return makeToken(identifierType());
}

Token typeVariable() {
  while (isTypeAlpha(peek()) || isDigit(peek()) || peek() == '\'') advance();

  if (isAlpha(peek()) || isSymbol(peek())) return identifier();

  return makeToken(TOKEN_TYPE_VARIABLE);
}

static Token number() {
  while (isDigit(peek())) advance();

  // look for a fractional part.
  if (peek() == '.' && isDigit(peekNext())) {
    // consume the '.'.
    advance();

    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}

Token string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '#' && peekNext() == '{') {
      advance();
      advance();
      return makeToken(TOKEN_INTERPOLATION);
    }

    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  // The closing quote.
  advance();
  return makeToken(TOKEN_STRING);
}

Token consumeToken(char c) {
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
    case '.': {
      if (match('.')) {
        return makeToken(TOKEN_DOUBLE_DOT);
      } else {
        return makeToken(TOKEN_DOT);
      }
    }
    case '|': {
      if (!isWhite(prev()) && !isWhite(peekNext()))
        return makeToken(TOKEN_PIPE);
      break;
    }
    case '!':
      return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=': {
      if (match('>')) {
        return makeToken(TOKEN_FAT_ARROW);
      } else {
        return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
      }
    }
    case '<': {
      if (match('-')) return makeToken(TOKEN_ARROW_LEFT);
      break;
    }
    case '"':
      return string();
  }

  if (isTypeAlpha(c)) return typeVariable();
  if (isAlpha(c) || isSymbol(c)) return identifier();
  if (isDigit(c)) return number();

  return errorToken("Unexpected character.");
}

Token scanVirtualToken(char c) {
  if (isAtEnd()) return makeToken(TOKEN_EOF);

  return consumeToken(c);
}

Token scanSlashedIdentifier() {
  while (isAlpha(peek()) || isDigit(peek()) || peek() == '/') advance();
  return scanner.current == scanner.start ? errorToken("Unexpected character.")
                                          : makeToken(TOKEN_IDENTIFIER);
}

Token scanToken() {
  skipWhitespace();
  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  return consumeToken(c);
}
