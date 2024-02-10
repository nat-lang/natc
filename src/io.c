#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

char* qualifyPath(const char* path) {
  static char buf[256];

  snprintf(buf, sizeof(buf), "%s%s", path, NAT_EXT);

  return buf;
}

char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

// Load and compile module at [path] without executing.
void compileFile(const char* path) {
  char* source = readFile(path);
  compile(source, (char*)path);
  free(source);
}

// Load, compile, and execute module at [path].
InterpretResult interpretFile(const char* path) {
  char* qualifiedPath = qualifyPath(path);
  char* source = readFile(qualifiedPath);

  InterpretResult result = interpret((char*)path, source);
  free(source);

  return result;
}
