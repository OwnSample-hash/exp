#include "/tmp/new_mod.h"

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __MAX_SIZE
#define __MAX_SIZE 40960
#endif

typedef struct {
  const char *name;
  const char *in_file[16];
  const char contents[__MAX_SIZE + 1];
} FileTemplate_t;

const FileTemplate_t file_templates[] = {
    {
        "CMakeLists.txt",
        {
            CONFIG_NEW_MODULE_UP_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_UP_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_UP_NAME,
            NULL,
        },
#embed "../modules/template/CMakeLists.txt"
    },
    {
        "tests/CMakeLists.txt",
        {
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_UP_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            NULL,
        },
#embed "../modules/template/tests/CMakeLists.txt"
    },
    {
        "tests/test_" CONFIG_NEW_MODULE_NAME ".cpp",
        {

            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            NULL,
        },
#embed "../modules/template/tests/test_template.cpp"
    },
    {
        "src/" CONFIG_NEW_MODULE_NAME ".cpp",
        {
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            NULL,
        },
#embed "../modules/template/src/template.cpp"
    },
    {
        "include/" CONFIG_NEW_MODULE_NAME ".hpp",
        {
            CONFIG_NEW_MODULE_NAME,
            NULL,
        },
#embed "../modules/template/include/template.hpp"
    },
    {
        "config.yaml",
        {
            CONFIG_NEW_MODULE_UP_NAME,
            CONFIG_NEW_MODULE_NAME,
            CONFIG_NEW_MODULE_NAME,
            NULL,
        },
#embed "../modules/template/config.yaml"
    },
    {
        NULL,
        NULL,
        0,
    } // Sentinel to mark the end of the array
};

const char *BASE_PATH = "modules/" CONFIG_NEW_MODULE_NAME "/";

size_t get_memory_size_for_formatting(const FileTemplate_t *template) {
  size_t size = strlen(template->contents) + 1; // +1 for null terminator
  for (int i = 0; template->in_file[i] != NULL; i++) {
    size += strlen(template->in_file[i]);
  }
  return size;
}

int main(int argc, const char **argv) {
  printf("Creating new module: %s\n", CONFIG_NEW_MODULE_NAME);
  printf("Base path: %s\n", BASE_PATH);
  printf("Max file size: %d bytes\n", __MAX_SIZE);
  struct stat st = {0};
  if (stat(BASE_PATH, &st) == 0) {
    fprintf(stderr, "Error: Module directory already exists: %s\n", BASE_PATH);
    return 1;
  }
  for (int i = 0; file_templates[i].name != NULL; i++) {
    const FileTemplate_t *template = file_templates + i;
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", BASE_PATH, template->name);

    // Create necessary directories
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s", full_path);
    for (char *p = dir_path; *p; p++) {
      if (*p == '/') {
        *p = '\0';
        int ret = mkdir(dir_path, 0755);
        if (ret != 0 && errno != EEXIST) {
          perror("Failed to create directory");
          return 1;
        }
        *p = '/';
      }
    }

    if (template->in_file[0] == NULL) {
      // Write the file
      FILE *file = fopen(full_path, "w");
      if (file == NULL) {
        perror("Failed to create file");
        return 1;
      }
      fwrite(template->contents, 1, strlen(template->contents), file);
      fclose(file);
      printf("Created: %s\n", full_path);
    } else {
      FILE *file = fopen(full_path, "w");
      if (file == NULL) {
        perror("Failed to create file");
        fprintf(stderr, "Full path: %s\n", full_path);
        return 1;
      }
      size_t needed_size = get_memory_size_for_formatting(template);
      char *buffer = calloc(needed_size, sizeof(char));
      if (buffer == NULL) {
        perror("Failed to allocate memory");
        fclose(file);
        return 1;
      }
      snprintf(buffer, needed_size, template->contents, template->in_file[0],
               template->in_file[1], template->in_file[2], template->in_file[3],
               template->in_file[4], template->in_file[5], template->in_file[6],
               template->in_file[7], template->in_file[8], template->in_file[9],
               template->in_file[10], template->in_file[11],
               template->in_file[12], template->in_file[13],
               template->in_file[14], template->in_file[15]);
      fwrite(buffer, 1, strlen(buffer), file);
      free(buffer);

      fclose(file);
      printf("Created in file: %s\n", full_path);
    }
  }
  return 0;
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
