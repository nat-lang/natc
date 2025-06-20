#include <libgen.h>
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

bool fileExists(const char* filename) {
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

char* pathToUri(const char* dirName, const char* baseName) {
  static char uri[256];

  snprintf(uri, sizeof(uri), "%s%s%s", dirName == NULL ? "" : dirName,
           dirName == NULL ? "" : "/", baseName);

  if (fileExists(uri)) return uri;

  char* withExt = withNatExt(uri);
  if (fileExists(withExt)) return withExt;

  return uri;
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
