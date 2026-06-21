#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HBM_CHANNELS = 16,
  PAIR_LANES = 8,
};

typedef struct {
  int *data;
  size_t size;
} IntArray;

typedef struct {
  double *data;
  size_t size;
} DoubleArray;

typedef struct {
  uint32_t word[5];
} Word130;

typedef struct {
  Word130 *data;
  size_t size;
  size_t capacity;
} WordArray;

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

static uint32_t float_bits(float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static Word130 pack_scalar(int done,
                           uint32_t packet_idx,
                           uint32_t pair_lane,
                           uint32_t scalar_lane,
                           uint32_t value) {
  Word130 word;
  memset(&word, 0, sizeof(word));
  word.word[0] = (packet_idx << 1) | (done ? 1u : 0u);
  word.word[1] = (packet_idx >> 31) | (pair_lane << 1);
  word.word[2] = (pair_lane >> 31) | (scalar_lane << 1);
  word.word[3] = (scalar_lane >> 31) | (value << 1);
  word.word[4] = value >> 31;
  return word;
}

static void append_word(WordArray *array, Word130 word) {
  if (array->size == array->capacity) {
    size_t next_capacity = array->capacity == 0 ? 16 : array->capacity * 2;
    Word130 *next = realloc(array->data, next_capacity * sizeof(*array->data));
    if (next == NULL) {
      die("allocation failed");
    }
    array->data = next;
    array->capacity = next_capacity;
  }
  array->data[array->size++] = word;
}

static FILE *open_output(const char *path) {
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    die_errno("failed to open", path);
  }
  return file;
}

static void write_word(FILE *file, const Word130 *word) {
  fprintf(file,
          "%08x%08x%08x%08x%08x\n",
          word->word[4],
          word->word[3],
          word->word[2],
          word->word[1],
          word->word[0]);
}

static int parse_positive_int(const char *name, const char *value) {
  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 0);
  if (errno != 0 || end == value || *end != '\0' || parsed <= 0) {
    fprintf(stderr, "invalid %s: %s\n", name, value);
    exit(2);
  }
  return (int)parsed;
}

int main(int argc, char **argv) {
  const char *matrix_dir = NULL;
  const char *out_dir = "build/backend_xsim_vectors";
  int iteration_num = 1;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--matrix") == 0 && i + 1 < argc) {
      matrix_dir = argv[++i];
    } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (strcmp(argv[i], "--iteration-num") == 0 && i + 1 < argc) {
      iteration_num = parse_positive_int("--iteration-num", argv[++i]);
    } else {
      fprintf(stderr,
              "usage: %s --matrix CSR_DIR --out-dir DIR "
              "[--iteration-num N]\n",
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

  IntArray row_ptr = read_int_array(row_ptr_path);
  IntArray col_idx = read_int_array(col_idx_path);
  DoubleArray values = read_double_array(values_path);

  free(row_ptr_path);
  free(col_idx_path);
  free(values_path);

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

  WordArray streams[HBM_CHANNELS][PAIR_LANES];
  memset(streams, 0, sizeof(streams));

  float *expected = calloc((size_t)rows, sizeof(*expected));
  if (expected == NULL) {
    die("allocation failed");
  }

  for (int iter = 0; iter < iteration_num; ++iter) {
    for (int row = 0; row < rows; ++row) {
      const int begin = row_ptr.data[row];
      const int end = row_ptr.data[row + 1];
      if (begin > end || begin < 0 || end > nnz) {
        die("row_ptr must be nondecreasing and within nnz");
      }

      const uint32_t packet_idx = (uint32_t)row >> 4;
      const uint32_t pair_lane = ((uint32_t)row >> 1) & 7u;
      const uint32_t scalar_lane = (uint32_t)row & 1u;
      const uint32_t owner = packet_idx % HBM_CHANNELS;

      for (int offset = begin; offset < end; ++offset) {
        const int col = col_idx.data[offset];
        if (col < 0 || col >= rows) {
          die("column index is outside row range");
        }

        const float product = (float)values.data[offset];
        if (iter == 0) {
          expected[row] += product;
        }
        append_word(&streams[owner][pair_lane],
                    pack_scalar(0,
                                packet_idx,
                                pair_lane,
                                scalar_lane,
                                float_bits(product)));
      }
    }
  }

  size_t max_stream_words = 0;
  for (int owner = 0; owner < HBM_CHANNELS; ++owner) {
    for (int lane = 0; lane < PAIR_LANES; ++lane) {
      append_word(&streams[owner][lane],
                  pack_scalar(1, (uint32_t)owner, (uint32_t)lane, 0, 0));
      if (streams[owner][lane].size > max_stream_words) {
        max_stream_words = streams[owner][lane].size;
      }
    }
  }

  char path[4096];
  for (int owner = 0; owner < HBM_CHANNELS; ++owner) {
    for (int lane = 0; lane < PAIR_LANES; ++lane) {
      snprintf(path,
               sizeof(path),
               "%s/owner%02d_lane%d.mem",
               out_dir,
               owner,
               lane);
      FILE *file = open_output(path);
      for (size_t idx = 0; idx < streams[owner][lane].size; ++idx) {
        write_word(file, &streams[owner][lane].data[idx]);
      }
      fclose(file);
    }
  }

  snprintf(path, sizeof(path), "%s/expected_y.mem", out_dir);
  FILE *expected_file = open_output(path);
  for (int row = 0; row < rows; ++row) {
    fprintf(expected_file, "%08x\n", float_bits(expected[row]));
  }
  fclose(expected_file);

  snprintf(path, sizeof(path), "%s/counts.mem", out_dir);
  FILE *counts_file = open_output(path);
  for (int owner = 0; owner < HBM_CHANNELS; ++owner) {
    for (int lane = 0; lane < PAIR_LANES; ++lane) {
      fprintf(counts_file, "%08zx\n", streams[owner][lane].size);
    }
  }
  fclose(counts_file);

  snprintf(path, sizeof(path), "%s/meta.env", out_dir);
  FILE *meta = open_output(path);
  fprintf(meta, "ROWS=%d\n", rows);
  fprintf(meta, "NNZ=%d\n", nnz);
  fprintf(meta, "ITERATION_NUM=%d\n", iteration_num);
  fprintf(meta, "NUM_OUT_PACKETS=%d\n", (rows + 15) / 16);
  fprintf(meta, "TAGGED_PAIRS_TOTAL=%d\n", ((rows + 15) / 16) * 8 * iteration_num);
  fprintf(meta, "SCALAR_WRITES_TOTAL=%d\n", ((rows + 15) / 16) * 16 * iteration_num);
  fprintf(meta, "MAX_STREAM_WORDS=%zu\n", max_stream_words);
  fclose(meta);

  for (int owner = 0; owner < HBM_CHANNELS; ++owner) {
    for (int lane = 0; lane < PAIR_LANES; ++lane) {
      free(streams[owner][lane].data);
    }
  }
  free(expected);
  free(row_ptr.data);
  free(col_idx.data);
  free(values.data);
  return 0;
}
