#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static int parse_int_arg(const char *name, const char *value) {
  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 0);
  if (errno != 0 || end == value || *end != '\0' || parsed <= 0) {
    fprintf(stderr, "invalid %s: %s\n", name, value);
    exit(2);
  }
  return (int)parsed;
}

static const char *parse_string_arg(const char *name, const char *value) {
  if (value == NULL || value[0] == '\0') {
    fprintf(stderr, "invalid %s\n", name);
    exit(2);
  }
  return value;
}

static FILE *open_output(const char *path) {
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
    exit(1);
  }
  return file;
}

int main(int argc, char **argv) {
  int rows = 128;
  int events = 4096;
  int entries = 8;
  int timeout_cycles = 200000;
  uint32_t seed = 1;
  const char *input_path = "build/input.mem";
  const char *expected_path = "build/expected.mem";
  const char *config_path = "build/ooo_accum_config.vh";

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
      rows = parse_int_arg("--rows", argv[++i]);
    } else if (strcmp(argv[i], "--events") == 0 && i + 1 < argc) {
      events = parse_int_arg("--events", argv[++i]);
    } else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) {
      entries = parse_int_arg("--entries", argv[++i]);
    } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
      timeout_cycles = parse_int_arg("--timeout", argv[++i]);
    } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = (uint32_t)parse_int_arg("--seed", argv[++i]);
    } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_path = parse_string_arg("--input", argv[++i]);
    } else if (strcmp(argv[i], "--expected") == 0 && i + 1 < argc) {
      expected_path = parse_string_arg("--expected", argv[++i]);
    } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = parse_string_arg("--config", argv[++i]);
    } else {
      fprintf(stderr,
              "usage: %s [--rows N] [--events N] [--entries N] "
              "[--seed N] [--timeout N] [--input PATH] "
              "[--expected PATH] [--config PATH]\n",
              argv[0]);
      return 2;
    }
  }

  if (rows > 65536) {
    fprintf(stderr, "--rows must fit in the 16-bit testbench row field\n");
    return 2;
  }

  int64_t *sum = calloc((size_t)rows, sizeof(*sum));
  uint8_t *touched = calloc((size_t)rows, sizeof(*touched));
  if (sum == NULL || touched == NULL) {
    fprintf(stderr, "allocation failed\n");
    return 1;
  }

  FILE *input = open_output(input_path);

  int hot_rows = rows < 32 ? rows : 32;
  for (int i = 0; i < events; ++i) {
    uint32_t rnd = xorshift32(&seed);
    int row;
    if ((i & 15) < 10) {
      row = ((i >> 2) + (int)(rnd & 3U)) % hot_rows;
    } else if ((i % 29) == 0) {
      row = ((i / 29) * 17) % rows;
    } else {
      row = (int)(rnd % (uint32_t)rows);
    }

    int value = (int)((rnd >> 8) % 17U) - 8;
    if (value == 0) {
      value = (i & 1) ? 1 : -1;
    }

    sum[row] += value;
    touched[row] = 1;

    uint64_t packed = ((uint64_t)0 << 48) |
                      ((uint64_t)(uint32_t)row << 32) |
                      (uint32_t)value;
    fprintf(input, "%013llx\n", (unsigned long long)packed);
  }

  uint64_t last = ((uint64_t)1 << 48);
  fprintf(input, "%013llx\n", (unsigned long long)last);
  fclose(input);

  int expected_count = 0;
  FILE *expected = open_output(expected_path);
  for (int row = 0; row < rows; ++row) {
    if (touched[row]) {
      int32_t value = (int32_t)sum[row];
      uint64_t packed = ((uint64_t)(uint32_t)row << 32) | (uint32_t)value;
      fprintf(expected, "%012llx\n", (unsigned long long)packed);
      ++expected_count;
    }
  }
  fclose(expected);

  FILE *config = open_output(config_path);
  fprintf(config, "`ifndef OOO_ACC_GENERATED_CONFIG_VH\n");
  fprintf(config, "`define OOO_ACC_GENERATED_CONFIG_VH\n");
  fprintf(config, "`define OOO_ACC_ROW_COUNT %d\n", rows);
  fprintf(config, "`define OOO_ACC_ENTRY_NUM %d\n", entries);
  fprintf(config, "`define OOO_ACC_INPUT_COUNT %d\n", events + 1);
  fprintf(config, "`define OOO_ACC_EXPECT_COUNT %d\n", expected_count);
  fprintf(config, "`define OOO_ACC_TIMEOUT_CYCLES %d\n", timeout_cycles);
  fprintf(config, "`endif\n");
  fclose(config);

  printf("generated rows=%d events=%d entries=%d expected_rows=%d\n",
         rows, events, entries, expected_count);

  free(sum);
  free(touched);
  return 0;
}
