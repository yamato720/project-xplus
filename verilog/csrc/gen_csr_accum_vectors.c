#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int *data;
  size_t size;
} IntArray;

typedef struct {
  double *data;
  size_t size;
} DoubleArray;

static void die(const char *message) {
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static void die_errno(const char *prefix, const char *path) {
  fprintf(stderr, "%s %s: %s\n", prefix, path, strerror(errno));
  exit(1);
}

static char *join_path(const char *dir, const char *name) {
  const size_t dir_len = strlen(dir);
  const size_t name_len = strlen(name);
  const int need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
  char *path = malloc(dir_len + (size_t)need_slash + name_len + 1);
  if (path == NULL) {
    die("allocation failed");
  }
  memcpy(path, dir, dir_len);
  if (need_slash) {
    path[dir_len] = '/';
  }
  memcpy(path + dir_len + (size_t)need_slash, name, name_len + 1);
  return path;
}

static IntArray read_int_array(const char *path) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    die_errno("failed to open", path);
  }

  size_t capacity = 1024;
  IntArray array;
  array.data = malloc(capacity * sizeof(*array.data));
  array.size = 0;
  if (array.data == NULL) {
    die("allocation failed");
  }

  int value = 0;
  while (fscanf(file, "%d", &value) == 1) {
    if (array.size == capacity) {
      capacity *= 2;
      int *next = realloc(array.data, capacity * sizeof(*array.data));
      if (next == NULL) {
        die("allocation failed");
      }
      array.data = next;
    }
    array.data[array.size++] = value;
  }

  fclose(file);
  return array;
}

static DoubleArray read_double_array(const char *path) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    die_errno("failed to open", path);
  }

  size_t capacity = 1024;
  DoubleArray array;
  array.data = malloc(capacity * sizeof(*array.data));
  array.size = 0;
  if (array.data == NULL) {
    die("allocation failed");
  }

  double value = 0.0;
  while (fscanf(file, "%lf", &value) == 1) {
    if (array.size == capacity) {
      capacity *= 2;
      double *next = realloc(array.data, capacity * sizeof(*array.data));
      if (next == NULL) {
        die("allocation failed");
      }
      array.data = next;
    }
    array.data[array.size++] = value;
  }

  fclose(file);
  return array;
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

static double parse_double_arg(const char *name, const char *value) {
  char *end = NULL;
  errno = 0;
  double parsed = strtod(value, &end);
  if (errno != 0 || end == value || *end != '\0' || parsed <= 0.0) {
    fprintf(stderr, "invalid %s: %s\n", name, value);
    exit(2);
  }
  return parsed;
}

static int row_width_for_count(int rows) {
  int width = 1;
  int64_t limit = 2;
  while (limit < rows) {
    limit <<= 1;
    ++width;
  }
  return width;
}

static int32_t quantize(double value, double scale) {
  const double scaled = nearbyint(value * scale);
  if (scaled > 2147483647.0) {
    return INT32_MAX;
  }
  if (scaled < -2147483648.0) {
    return INT32_MIN;
  }
  return (int32_t)scaled;
}

static int32_t wrap_add_i32(int32_t lhs, int32_t rhs) {
  return (int32_t)((uint32_t)lhs + (uint32_t)rhs);
}

static FILE *open_output(const char *path) {
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    die_errno("failed to open", path);
  }
  return file;
}

int main(int argc, char **argv) {
  const char *matrix_dir = NULL;
  const char *input_path = "build/csr_input.tsv";
  const char *expected_path = "build/csr_expected.tsv";
  const char *meta_path = "build/csr_meta.env";
  int entries = 16;
  double scale = 1024.0;
  int drop_diagonal = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--matrix") == 0 && i + 1 < argc) {
      matrix_dir = argv[++i];
    } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_path = argv[++i];
    } else if (strcmp(argv[i], "--expected") == 0 && i + 1 < argc) {
      expected_path = argv[++i];
    } else if (strcmp(argv[i], "--meta") == 0 && i + 1 < argc) {
      meta_path = argv[++i];
    } else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) {
      entries = parse_int_arg("--entries", argv[++i]);
    } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      scale = parse_double_arg("--scale", argv[++i]);
    } else if (strcmp(argv[i], "--drop-diagonal") == 0) {
      drop_diagonal = 1;
    } else {
      fprintf(stderr,
              "usage: %s --matrix CSR_DIR [--input PATH] [--expected PATH] "
              "[--meta PATH] [--entries N] [--scale S] [--drop-diagonal]\n",
              argv[0]);
      return 2;
    }
  }

  if (matrix_dir == NULL) {
    die("--matrix CSR_DIR is required");
  }

  char *row_ptr_path = join_path(matrix_dir, "row_ptr.txt");
  char *col_idx_path = join_path(matrix_dir, "col_idx.txt");
  char *values_path = join_path(matrix_dir, "values.txt");
  char *x0_path = join_path(matrix_dir, "x0.txt");

  IntArray row_ptr = read_int_array(row_ptr_path);
  IntArray col_idx = read_int_array(col_idx_path);
  DoubleArray values = read_double_array(values_path);
  DoubleArray x0 = read_double_array(x0_path);

  free(row_ptr_path);
  free(col_idx_path);
  free(values_path);
  free(x0_path);

  if (row_ptr.size < 2) {
    die("row_ptr.txt must contain at least two entries");
  }

  const int rows = (int)row_ptr.size - 1;
  const int nnz = row_ptr.data[rows];
  if (row_ptr.data[0] != 0) {
    die("row_ptr[0] must be 0");
  }
  if (nnz < 0 || (size_t)nnz != col_idx.size || (size_t)nnz != values.size) {
    die("CSR nnz mismatch between row_ptr/col_idx/values");
  }
  if (x0.size < (size_t)rows) {
    die("x0.txt is shorter than the row count");
  }

  int32_t *expected = calloc((size_t)rows, sizeof(*expected));
  uint8_t *touched = calloc((size_t)rows, sizeof(*touched));
  if (expected == NULL || touched == NULL) {
    die("allocation failed");
  }

  FILE *input = open_output(input_path);

  int64_t events = 0;
  for (int row = 0; row < rows; ++row) {
    const int begin = row_ptr.data[row];
    const int end = row_ptr.data[row + 1];
    if (begin > end || begin < 0 || end > nnz) {
      die("row_ptr must be nondecreasing and within nnz");
    }
    for (int offset = begin; offset < end; ++offset) {
      const int col = col_idx.data[offset];
      if (col < 0 || col >= rows) {
        die("column index is outside x0 range");
      }
      if (drop_diagonal && col == row) {
        continue;
      }

      const int32_t value = quantize(values.data[offset] * x0.data[col], scale);
      expected[row] = wrap_add_i32(expected[row], value);
      touched[row] = 1;
      fprintf(input, "0 %d %d\n", row, value);
      ++events;
    }
  }
  fprintf(input, "1 0 0\n");
  fclose(input);

  int expected_rows = 0;
  FILE *expected_file = open_output(expected_path);
  for (int row = 0; row < rows; ++row) {
    if (touched[row]) {
      fprintf(expected_file, "%d %d\n", row, expected[row]);
      ++expected_rows;
    }
  }
  fclose(expected_file);

  FILE *meta = open_output(meta_path);
  fprintf(meta, "CSR_ROWS=%d\n", rows);
  fprintf(meta, "CSR_ROW_WIDTH=%d\n", row_width_for_count(rows));
  fprintf(meta, "CSR_EVENTS=%lld\n", (long long)events);
  fprintf(meta, "CSR_INPUT_COUNT=%lld\n", (long long)events + 1);
  fprintf(meta, "CSR_EXPECT_COUNT=%d\n", expected_rows);
  fprintf(meta, "CSR_ENTRIES=%d\n", entries);
  fprintf(meta, "CSR_SCALE=%.17g\n", scale);
  fprintf(meta, "CSR_DROP_DIAGONAL=%d\n", drop_diagonal);
  fclose(meta);

  printf("generated matrix=%s rows=%d nnz=%d events=%lld expected_rows=%d "
         "row_width=%d entries=%d scale=%.17g drop_diagonal=%d\n",
         matrix_dir,
         rows,
         nnz,
         (long long)events,
         expected_rows,
         row_width_for_count(rows),
         entries,
         scale,
         drop_diagonal);

  free(row_ptr.data);
  free(col_idx.data);
  free(values.data);
  free(x0.data);
  free(expected);
  free(touched);
  return 0;
}
