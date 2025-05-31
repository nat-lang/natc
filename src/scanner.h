#ifndef nat_scanner_h
#define nat_scanner_h

#include "common.h"

typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

Scanner initScanner(const char* source);
Scanner saveScanner();
void initToken(Token* token);
Token syntheticToken(const char* start);
void gotoScanner(Scanner scanner);
void rewindScanner(Token token);
void printScanner(Scanner sc);
void skipWhitespace();
Token scanToken();
Token scanPathIdentifier();
Token scanVirtualToken(char c);
char charAt(int i);
bool isWhite(char c);

#endif