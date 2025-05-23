#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

char* withNatExt(const char* path) {
  static char buf[256];

  snprintf(buf, sizeof(buf), "%s%s", path, NAT_EXT);

  return buf;
}

char* pathToUri(const char* path) {
  static char buf[256];

  snprintf(buf, sizeof(buf), "%s%s", NAT_SRC_DIR, path);

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

bool fileExists(const char* filename) {
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

char* readSource(const char* path) {
  if (fileExists(path))
    return readFile(path);
  else
    return readFile(withNatExt(path));
}

char* readRelativeSource(const char* path) {
  char* absolutePath = pathToUri(path);
  return readSource(absolutePath);
}