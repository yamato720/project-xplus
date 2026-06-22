// Pure-C profiler for the Cuper/Jacobi SpElement packing layout.
//
// It reads one CSR text dataset directory, maps entries to the same HBM/PE
// buckets used by the Jacobi host packer, simulates the per-PE reordering
// window, and reports how many 512-bit matrix beats the hardware would read.

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kPeNum = 8,
  kRowHbmNum = 4,
  kDefaultWindow = 10,
};

typedef struct {
  int row;
  int col;
  float val;
} Element;

typedef struct {
  size_t count;
  size_t fill;
  size_t sched_len;
  Element* elems;
} Bucket;

typedef struct {
  int valid;
  Element elem;
} Slot;

typedef struct {
  char name[256];
  int m;
  int n;
  size_t nnz;
} Meta;

typedef struct {
  int hbm[8];
  int hbm_count;
  int drop_diag;
  int top_batches;
  int dump_batch;
  int dump_beats;
  int csv;
  int window;
} Options;

typedef struct {
  int id;
  size_t real;
  size_t scheduled;
  size_t max_len;
  size_t channel_len_sum;
  size_t channel_len_min;
  size_t channel_len_max;
  size_t channel_len_ideal;
  size_t channel_len_extra;
  size_t read_slots;
  size_t channel_read_slots;
  size_t pe_compact_read_slots;
  size_t lane_static_real_slots;
  size_t real_compact_read_slots;
  double density;
  double channel_density;
  double channel_balance_eff;
  double pe_compact_density;
  double lane_static_real_density;
  double real_compact_density;
} BatchStat;

static void die(const char* msg) {
  fprintf(stderr, "pack_profile: %s\n", msg);
  exit(1);
}

static void die_errno(const char* msg) {
  fprintf(stderr, "pack_profile: %s: %s\n", msg, strerror(errno));
  exit(1);
}

static void* xcalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) die_errno("calloc failed");
  return p;
}

static void* xmalloc(size_t size) {
  void* p = malloc(size);
  if (!p) die_errno("malloc failed");
  return p;
}

static char* join_path(const char* dir, const char* file) {
  const size_t a = strlen(dir);
  const size_t b = strlen(file);
  const int need_slash = (a > 0 && dir[a - 1] != '/');
  char* out = (char*)xmalloc(a + need_slash + b + 1);
  memcpy(out, dir, a);
  if (need_slash) out[a] = '/';
  memcpy(out + a + need_slash, file, b + 1);
  return out;
}

static char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) ++s;
  char* end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) --end;
  *end = '\0';
  return s;
}

static int parse_int(const char* s, int* out) {
  char* end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno != 0 || end == s) return 0;
  while (*end && isspace((unsigned char)*end)) ++end;
  if (*end) return 0;
  if (v < 0 || v > 2147483647L) return 0;
  *out = (int)v;
  return 1;
}

static int parse_size(const char* s, size_t* out) {
  char* end = NULL;
  errno = 0;
  unsigned long long v = strtoull(s, &end, 10);
  if (errno != 0 || end == s) return 0;
  while (*end && isspace((unsigned char)*end)) ++end;
  if (*end) return 0;
  *out = (size_t)v;
  return 1;
}

static Meta read_meta(const char* dir) {
  Meta meta;
  memset(&meta, 0, sizeof(meta));
  char* path = join_path(dir, "meta.txt");
  FILE* f = fopen(path, "r");
  if (!f) die_errno(path);

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = trim(line);
    char* val = trim(eq + 1);
    if (strcmp(key, "name") == 0) {
      snprintf(meta.name, sizeof(meta.name), "%s", val);
    } else if (strcmp(key, "m") == 0) {
      if (!parse_int(val, &meta.m)) die("bad m in meta.txt");
    } else if (strcmp(key, "n") == 0) {
      if (!parse_int(val, &meta.n)) die("bad n in meta.txt");
    } else if (strcmp(key, "nnz") == 0) {
      if (!parse_size(val, &meta.nnz)) die("bad nnz in meta.txt");
    }
  }
  fclose(f);
  free(path);

  if (meta.m <= 0 || meta.n <= 0 || meta.nnz == 0) {
    die("meta.txt must provide positive m, n, and nnz");
  }
  if (meta.name[0] == '\0') snprintf(meta.name, sizeof(meta.name), "%s", dir);
  return meta;
}

static int* read_int_file_exact(const char* dir, const char* name, size_t count) {
  char* path = join_path(dir, name);
  FILE* f = fopen(path, "r");
  if (!f) die_errno(path);
  int* data = (int*)xmalloc(count * sizeof(int));
  for (size_t i = 0; i < count; ++i) {
    long long v = 0;
    if (fscanf(f, "%lld", &v) != 1) {
      fprintf(stderr, "pack_profile: expected %zu integers in %s, stopped at %zu\n",
              count, path, i);
      exit(1);
    }
    if (v < 0 || v > 2147483647LL) die("integer value out of 32-bit range");
    data[i] = (int)v;
  }
  fclose(f);
  free(path);
  return data;
}

static float* read_float_file_optional(const char* dir, const char* name, size_t count) {
  char* path = join_path(dir, name);
  FILE* f = fopen(path, "r");
  if (!f) {
    free(path);
    return NULL;
  }
  float* data = (float*)xmalloc(count * sizeof(float));
  for (size_t i = 0; i < count; ++i) {
    if (fscanf(f, "%f", &data[i]) != 1) {
      fprintf(stderr, "pack_profile: expected %zu floats in %s, stopped at %zu\n",
              count, path, i);
      exit(1);
    }
  }
  fclose(f);
  free(path);
  return data;
}

static int cmp_element_col_row(const void* a, const void* b) {
  const Element* x = (const Element*)a;
  const Element* y = (const Element*)b;
  if (x->col < y->col) return -1;
  if (x->col > y->col) return 1;
  if (x->row < y->row) return -1;
  if (x->row > y->row) return 1;
  return 0;
}

static size_t map_pe(int row, int hbm) {
  const size_t packet_id = (size_t)row / 2u;
  const size_t group_size = (size_t)hbm / 8u;
  const size_t checker_id = packet_id % 8u;
  const size_t acc_offset = (packet_id / 8u) % group_size;
  const size_t pe_in_acc = (packet_id / (size_t)hbm) % 8u;
  return (checker_id * group_size + acc_offset) * 8u + pe_in_acc;
}

static void ensure_occupied(char** occupied, size_t* cap, size_t need) {
  if (need < *cap) return;
  size_t next = (*cap == 0) ? 16 : *cap;
  while (next <= need) next *= 2;
  char* p = (char*)realloc(*occupied, next);
  if (!p) die_errno("realloc occupied failed");
  memset(p + *cap, 0, next - *cap);
  *occupied = p;
  *cap = next;
}

static size_t simulate_bucket(Bucket* bucket,
                              int row_num,
                              int num_pe,
                              int window,
                              size_t capture_limit,
                              Slot* capture) {
  if (bucket->count == 0) {
    bucket->sched_len = 0;
    return 0;
  }

  qsort(bucket->elems, bucket->count, sizeof(Element), cmp_element_col_row);

  const size_t org_rows = ((size_t)row_num + (size_t)(2 * num_pe) - 1u) /
                          (size_t)(2 * num_pe) + 1u;
  long long* sliding = (long long*)xmalloc(org_rows * sizeof(long long));
  for (size_t i = 0; i < org_rows; ++i) sliding[i] = -(long long)window;

  char* occupied = NULL;
  size_t occupied_cap = 0;
  size_t max_len = 0;

  for (size_t i = 0; i < bucket->count; ++i) {
    const Element e = bucket->elems[i];
    const size_t org = (size_t)e.row / (size_t)(2 * num_pe);
    if (org >= org_rows) die("internal row index out of range");

    long long pos_ll = sliding[org] + window;
    if (pos_ll < 0) pos_ll = 0;
    size_t pos = (size_t)pos_ll;

    for (;;) {
      ensure_occupied(&occupied, &occupied_cap, pos);
      if (!occupied[pos]) break;
      ++pos;
    }

    occupied[pos] = 1;
    sliding[org] = (long long)pos;
    if (pos + 1u > max_len) max_len = pos + 1u;
    if (capture && pos < capture_limit) {
      capture[pos].valid = 1;
      capture[pos].elem = e;
    }
  }

  free(occupied);
  free(sliding);
  bucket->sched_len = max_len;
  return max_len;
}

static uint64_t float_bits(float v) {
  uint32_t bits = 0;
  memcpy(&bits, &v, sizeof(bits));
  return (uint64_t)bits;
}

static uint64_t pack_slot(const Slot* slot, int batch, int slice_width, int num_pe) {
  if (!slot->valid) return 0x3ffffULL << 32;

  const Element e = slot->elem;
  const int local_col = e.col - batch * slice_width;
  const int org_row = e.row / (2 * num_pe);
  const int row_enc = org_row * 2 + (e.row & 1);
  const uint64_t x_col = ((uint64_t)local_col & 0x3fffULL) << 50;
  const uint64_t x_row = ((uint64_t)row_enc & 0x3ffffULL) << 32;
  return x_col | x_row | float_bits(e.val);
}

static void print_slot(const Slot* slot, int batch, int slice_width, int num_pe) {
  const uint64_t packed = pack_slot(slot, batch, slice_width, num_pe);
  if (!slot->valid) {
    printf(" empty:%013" PRIx64, packed);
    return;
  }
  const Element e = slot->elem;
  const int local_col = e.col - batch * slice_width;
  const int org_row = e.row / (2 * num_pe);
  const int row_enc = org_row * 2 + (e.row & 1);
  printf(" r%d c%d lc%d re%d:%013" PRIx64,
         e.row, e.col, local_col, row_enc, packed);
}

static int cmp_batch_padding(const void* a, const void* b) {
  const BatchStat* x = (const BatchStat*)a;
  const BatchStat* y = (const BatchStat*)b;
  const size_t px = x->read_slots - x->real;
  const size_t py = y->read_slots - y->real;
  if (px < py) return 1;
  if (px > py) return -1;
  if (x->density < y->density) return 1;
  if (x->density > y->density) return -1;
  return x->id - y->id;
}

static int cmp_batch_hbm_imbalance(const void* a, const void* b) {
  const BatchStat* x = (const BatchStat*)a;
  const BatchStat* y = (const BatchStat*)b;
  if (x->channel_len_extra < y->channel_len_extra) return 1;
  if (x->channel_len_extra > y->channel_len_extra) return -1;
  if (x->channel_balance_eff < y->channel_balance_eff) return -1;
  if (x->channel_balance_eff > y->channel_balance_eff) return 1;
  return x->id - y->id;
}

static size_t ceil_div_size(size_t value, size_t divisor) {
  return (value + divisor - 1u) / divisor;
}

static size_t sum_size_array(const size_t* data, int n) {
  size_t total = 0;
  for (int i = 0; i < n; ++i) total += data[i];
  return total;
}

static size_t min_size_array(const size_t* data, int n) {
  size_t min_v = data[0];
  for (int i = 1; i < n; ++i) {
    if (data[i] < min_v) min_v = data[i];
  }
  return min_v;
}

static size_t max_size_array(const size_t* data, int n) {
  size_t max_v = data[0];
  for (int i = 1; i < n; ++i) {
    if (data[i] > max_v) max_v = data[i];
  }
  return max_v;
}

static double balance_efficiency(size_t total, int lanes, size_t bottleneck) {
  if (lanes <= 0 || bottleneck == 0) return total == 0 ? 1.0 : 0.0;
  const long double denom = (long double)lanes * (long double)bottleneck;
  return denom > 0.0 ? (double)((long double)total / denom) : 0.0;
}

static void print_parallel_balance(const char* label, const size_t* data, int n) {
  const size_t total = sum_size_array(data, n);
  const size_t min_v = min_size_array(data, n);
  const size_t max_v = max_size_array(data, n);
  const size_t ideal = ceil_div_size(total, (size_t)n);
  const size_t extra = (max_v > ideal) ? max_v - ideal : 0;
  const double eff = balance_efficiency(total, n, max_v);
  const double idle = (eff < 1.0) ? (1.0 - eff) : 0.0;
  const double avg = n ? (double)total / (double)n : 0.0;
  printf("    %-24s total=%zu min=%zu max=%zu avg=%.2f ideal=%zu extra=%zu eff=%.2f%% idle=%.2f%%\n",
         label, total, min_v, max_v, avg, ideal, extra, eff * 100.0,
         idle * 100.0);
}

static void print_min_max_avg(const char* label, const size_t* data, int n) {
  size_t min_v = data[0];
  size_t max_v = data[0];
  long double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    if (data[i] < min_v) min_v = data[i];
    if (data[i] > max_v) max_v = data[i];
    sum += (long double)data[i];
  }
  const long double avg = sum / (long double)n;
  const long double ratio = (avg > 0.0) ? (long double)max_v / avg : 0.0;
  printf("  %-22s min=%zu max=%zu avg=%.2Lf max/avg=%.3Lf\n",
         label, min_v, max_v, avg, ratio);
}

static void print_csv_header(void) {
  printf("dataset,hbm,m,n,drop_diag,window,real_nnz,slice_size,batch_size,slice_width,"
         "batch_num,matrix_len,read_beats,read_slots,density,padding_slots,"
         "reorder_holes,batch_pad,channel_real_min,channel_real_max,"
         "pe_real_min,pe_real_max,channel_dyn_read_beats,"
         "channel_dyn_read_slots,channel_dyn_density,channel_dyn_padding_slots,"
         "channel_dyn_batch_pad,channel_dyn_savings_slots,"
         "channel_dyn_savings_pct,pe_lower_read_slots,pe_lower_density,"
         "channel_dyn_len_min,channel_dyn_len_max,"
         "pe_dyn_intra_hbm_savings_slots,pe_dyn_total_savings_slots,"
         "pe_dyn_total_savings_pct,pe_compact_batch_read_beats,"
         "pe_compact_batch_read_slots,pe_compact_batch_density,"
         "pe_compact_batch_align_pad,pe_compact_batch_total_savings_slots,"
         "pe_compact_batch_total_savings_pct,pe_compact_stream_read_beats,"
         "pe_compact_stream_read_slots,pe_compact_stream_density,"
         "pe_compact_stream_align_pad,pe_compact_stream_total_savings_slots,"
         "pe_compact_stream_total_savings_pct,pe_sched_len_min,"
         "pe_sched_len_max,pe_compact_stream_channel_beats_min,"
         "pe_compact_stream_channel_beats_max,lane_static_real_batch_read_beats,"
         "lane_static_real_batch_read_slots,lane_static_real_batch_density,"
         "lane_static_real_batch_total_savings_slots,"
         "lane_static_real_batch_total_savings_pct,"
         "lane_static_real_stream_read_beats,lane_static_real_stream_read_slots,"
         "lane_static_real_stream_density,"
         "lane_static_real_stream_total_savings_slots,"
         "lane_static_real_stream_total_savings_pct,"
         "real_compact_batch_read_beats,real_compact_batch_read_slots,"
         "real_compact_batch_density,real_compact_batch_align_pad,"
         "real_compact_batch_total_savings_slots,"
         "real_compact_batch_total_savings_pct,"
         "real_compact_stream_read_beats,real_compact_stream_read_slots,"
         "real_compact_stream_density,real_compact_stream_align_pad,"
         "real_compact_stream_total_savings_slots,"
         "real_compact_stream_total_savings_pct,"
         "balanced_compact_stream_read_beats,"
         "balanced_compact_stream_read_slots,balanced_compact_stream_density,"
         "balanced_compact_stream_total_savings_slots,"
         "balanced_compact_stream_total_savings_pct,"
         "balanced_compact_stream_channel_beats,"
         "channel_real_balance_eff,channel_sched_balance_eff,"
         "channel_dyn_balance_eff,pe_real_balance_eff,pe_sched_balance_eff,"
         "pe_compact_stream_hbm_balance_eff,channel_dyn_ideal_beats,"
         "channel_dyn_extra_beats,channel_dyn_bottleneck_beats,"
         "lane_static_real_hbm_balance_eff,lane_static_real_hbm_beats_min,"
         "lane_static_real_hbm_beats_max,lane_static_real_hbm_ideal_beats,"
         "lane_static_real_hbm_extra_beats\n");
}

static void profile_one_hbm(const Meta* meta,
                            const int* row_ptr,
                            const int* col_idx,
                            const float* values,
                            const Options* opt,
                            int hbm,
                            const char* dataset) {
  if (!(hbm == 16 || hbm == 24 || hbm == 32)) die("HBM must be 16, 24, or 32");

  const int num_pe = hbm * kPeNum;
  const int slice_size = hbm * kRowHbmNum;
  const int batch_size = 8192 / slice_size;
  const int slice_width = slice_size * batch_size;
  const int num_col_slices = (meta->n + slice_size - 1) / slice_size;
  const int batch_num = (num_col_slices + batch_size - 1) / batch_size;
  const size_t bucket_num = (size_t)batch_num * (size_t)num_pe;

  Bucket* buckets = (Bucket*)xcalloc(bucket_num, sizeof(Bucket));
  size_t real_nnz = 0;
  size_t diag_removed = 0;

  for (int row = 0; row < meta->m; ++row) {
    for (int j = row_ptr[row]; j < row_ptr[row + 1]; ++j) {
      const int col = col_idx[j];
      if (opt->drop_diag && row == col) {
        ++diag_removed;
        continue;
      }
      const int batch = (col / slice_size) / batch_size;
      if (batch < 0 || batch >= batch_num) die("column produced invalid batch");
      const size_t pe = map_pe(row, hbm);
      buckets[(size_t)batch * (size_t)num_pe + pe].count++;
      ++real_nnz;
    }
  }

  for (size_t i = 0; i < bucket_num; ++i) {
    if (buckets[i].count) {
      buckets[i].elems = (Element*)xmalloc(buckets[i].count * sizeof(Element));
    }
  }

  for (int row = 0; row < meta->m; ++row) {
    for (int j = row_ptr[row]; j < row_ptr[row + 1]; ++j) {
      const int col = col_idx[j];
      if (opt->drop_diag && row == col) continue;
      const int batch = (col / slice_size) / batch_size;
      const size_t pe = map_pe(row, hbm);
      Bucket* b = &buckets[(size_t)batch * (size_t)num_pe + pe];
      Element* e = &b->elems[b->fill++];
      e->row = row;
      e->col = col;
      e->val = values ? values[j] : 1.0f;
    }
  }

  BatchStat* batch_stats = (BatchStat*)xcalloc((size_t)batch_num, sizeof(BatchStat));
  size_t* channel_real = (size_t*)xcalloc((size_t)hbm, sizeof(size_t));
  size_t* channel_sched = (size_t*)xcalloc((size_t)hbm, sizeof(size_t));
  size_t* channel_dynamic_len = (size_t*)xcalloc((size_t)hbm, sizeof(size_t));
  size_t* channel_pe_compact_stream_beats = (size_t*)xcalloc((size_t)hbm, sizeof(size_t));
  size_t* channel_lane_static_real_beats = (size_t*)xcalloc((size_t)hbm, sizeof(size_t));
  size_t* pe_real = (size_t*)xcalloc((size_t)num_pe, sizeof(size_t));
  size_t* pe_sched = (size_t*)xcalloc((size_t)num_pe, sizeof(size_t));
  Slot* dump_slots = NULL;
  if (opt->dump_batch >= 0 && opt->dump_batch < batch_num && opt->dump_beats > 0) {
    dump_slots = (Slot*)xcalloc((size_t)num_pe * (size_t)opt->dump_beats, sizeof(Slot));
  }

  size_t matrix_len = 0;
  size_t scheduled_slots = 0;
  size_t channel_dynamic_read_beats = 0;
  // Future protocol estimate: compact each HBM channel's eight PE-lane streams
  // into dense 512-bit beats. This needs an explicit PE/lane tag or equivalent
  // row-low-bit reconstruction in hardware; the current fixed-lane packet format
  // cannot consume these packets directly.
  size_t pe_compact_batch_read_beats = 0;
  // Reorder-free estimates for a future protocol:
  //
  //   lane_static_real_* keeps slot p mapped to lane p, but only schedules real
  //   entries. It removes reorder holes while preserving a simple fixed-lane
  //   accumulator; remaining padding is lane imbalance.
  //
  //   real_compact_* packs only real entries into 512-bit beats. It is a lower
  //   bound for a dynamic/demux protocol that carries enough row/lane metadata.
  size_t lane_static_real_batch_read_beats = 0;
  size_t real_compact_batch_read_beats = 0;

  for (int batch = 0; batch < batch_num; ++batch) {
    size_t max_len = 0;
    size_t batch_real = 0;
    size_t batch_scheduled = 0;
    size_t channel_batch_len[32];
    size_t channel_batch_sched[32];
    size_t channel_batch_real[32];
    size_t channel_lane_real[32][kPeNum];
    for (int c = 0; c < hbm; ++c) {
      channel_batch_len[c] = 0;
      channel_batch_sched[c] = 0;
      channel_batch_real[c] = 0;
      for (int lane = 0; lane < kPeNum; ++lane) {
        channel_lane_real[c][lane] = 0;
      }
    }

    for (int pe = 0; pe < num_pe; ++pe) {
      Bucket* b = &buckets[(size_t)batch * (size_t)num_pe + (size_t)pe];
      Slot* capture = NULL;
      if (dump_slots && batch == opt->dump_batch) {
        capture = dump_slots + (size_t)pe * (size_t)opt->dump_beats;
      }
      const size_t sched = simulate_bucket(
          b, meta->m, num_pe, opt->window,
          (capture ? (size_t)opt->dump_beats : 0u), capture);
      if (sched > max_len) max_len = sched;
      const int channel = pe / kPeNum;
      if (sched > channel_batch_len[channel]) channel_batch_len[channel] = sched;
      channel_batch_sched[channel] += sched;
      channel_batch_real[channel] += b->count;
      channel_lane_real[channel][pe % kPeNum] = b->count;
      batch_real += b->count;
      batch_scheduled += sched;
      scheduled_slots += sched;

      pe_real[pe] += b->count;
      pe_sched[pe] += sched;
      channel_real[channel] += b->count;
      channel_sched[channel] += sched;
    }

    size_t channel_batch_read_beats = 0;
    size_t pe_compact_batch_beats = 0;
    size_t lane_static_real_batch_beats = 0;
    size_t real_compact_batch_beats = 0;
    size_t channel_batch_len_min = channel_batch_len[0];
    size_t channel_batch_len_max = channel_batch_len[0];
    for (int c = 0; c < hbm; ++c) {
      if (channel_batch_len[c] < channel_batch_len_min) {
        channel_batch_len_min = channel_batch_len[c];
      }
      if (channel_batch_len[c] > channel_batch_len_max) {
        channel_batch_len_max = channel_batch_len[c];
      }
      channel_dynamic_len[c] += channel_batch_len[c];
      channel_batch_read_beats += channel_batch_len[c];
      pe_compact_batch_beats += ceil_div_size(channel_batch_sched[c], (size_t)kPeNum);
      size_t channel_lane_max = 0;
      for (int lane = 0; lane < kPeNum; ++lane) {
        if (channel_lane_real[c][lane] > channel_lane_max) {
          channel_lane_max = channel_lane_real[c][lane];
        }
      }
      lane_static_real_batch_beats += channel_lane_max;
      channel_lane_static_real_beats[c] += channel_lane_max;
      real_compact_batch_beats += ceil_div_size(channel_batch_real[c], (size_t)kPeNum);
    }
    channel_dynamic_read_beats += channel_batch_read_beats;
    pe_compact_batch_read_beats += pe_compact_batch_beats;
    lane_static_real_batch_read_beats += lane_static_real_batch_beats;
    real_compact_batch_read_beats += real_compact_batch_beats;

    matrix_len += max_len;
    batch_stats[batch].id = batch;
    batch_stats[batch].real = batch_real;
    batch_stats[batch].scheduled = batch_scheduled;
    batch_stats[batch].max_len = max_len;
    batch_stats[batch].channel_len_sum = channel_batch_read_beats;
    batch_stats[batch].channel_len_min = channel_batch_len_min;
    batch_stats[batch].channel_len_max = channel_batch_len_max;
    batch_stats[batch].channel_len_ideal =
        ceil_div_size(channel_batch_read_beats, (size_t)hbm);
    batch_stats[batch].channel_len_extra =
        (channel_batch_len_max > batch_stats[batch].channel_len_ideal)
            ? channel_batch_len_max - batch_stats[batch].channel_len_ideal
            : 0;
    batch_stats[batch].read_slots = max_len * (size_t)num_pe;
    batch_stats[batch].channel_read_slots = channel_batch_read_beats * (size_t)kPeNum;
    batch_stats[batch].pe_compact_read_slots = pe_compact_batch_beats * (size_t)kPeNum;
    batch_stats[batch].lane_static_real_slots =
        lane_static_real_batch_beats * (size_t)kPeNum;
    batch_stats[batch].real_compact_read_slots =
        real_compact_batch_beats * (size_t)kPeNum;
    batch_stats[batch].density = batch_stats[batch].read_slots
                                     ? (double)batch_real /
                                           (double)batch_stats[batch].read_slots
                                     : 0.0;
    batch_stats[batch].channel_density =
        batch_stats[batch].channel_read_slots
            ? (double)batch_real / (double)batch_stats[batch].channel_read_slots
            : 0.0;
    batch_stats[batch].channel_balance_eff =
        balance_efficiency(channel_batch_read_beats, hbm, channel_batch_len_max);
    batch_stats[batch].pe_compact_density =
        batch_stats[batch].pe_compact_read_slots
            ? (double)batch_real / (double)batch_stats[batch].pe_compact_read_slots
            : 0.0;
    batch_stats[batch].lane_static_real_density =
        batch_stats[batch].lane_static_real_slots
            ? (double)batch_real / (double)batch_stats[batch].lane_static_real_slots
            : 0.0;
    batch_stats[batch].real_compact_density =
        batch_stats[batch].real_compact_read_slots
            ? (double)batch_real / (double)batch_stats[batch].real_compact_read_slots
            : 0.0;
  }

  const size_t read_beats = matrix_len * (size_t)hbm;
  const size_t read_slots = read_beats * (size_t)kPeNum;
  const size_t channel_dynamic_read_slots =
      channel_dynamic_read_beats * (size_t)kPeNum;
  const size_t pe_compact_batch_read_slots =
      pe_compact_batch_read_beats * (size_t)kPeNum;
  const size_t lane_static_real_batch_read_slots =
      lane_static_real_batch_read_beats * (size_t)kPeNum;
  const size_t real_compact_batch_read_slots =
      real_compact_batch_read_beats * (size_t)kPeNum;
  const size_t padding_slots = (read_slots >= real_nnz) ? read_slots - real_nnz : 0;
  const size_t channel_dynamic_padding_slots =
      (channel_dynamic_read_slots >= real_nnz) ? channel_dynamic_read_slots - real_nnz : 0;
  const size_t reorder_holes =
      (scheduled_slots >= real_nnz) ? scheduled_slots - real_nnz : 0;
  const size_t batch_pad =
      (read_slots >= scheduled_slots) ? read_slots - scheduled_slots : 0;
  const size_t channel_dynamic_batch_pad =
      (channel_dynamic_read_slots >= scheduled_slots) ? channel_dynamic_read_slots - scheduled_slots : 0;
  const size_t pe_dynamic_intra_hbm_savings_slots =
      (channel_dynamic_read_slots >= scheduled_slots) ? channel_dynamic_read_slots - scheduled_slots : 0;
  const size_t pe_dynamic_total_savings_slots =
      (read_slots >= scheduled_slots) ? read_slots - scheduled_slots : 0;
  const size_t channel_dynamic_savings_slots =
      (read_slots >= channel_dynamic_read_slots) ? read_slots - channel_dynamic_read_slots : 0;
  const size_t pe_compact_batch_align_pad =
      (pe_compact_batch_read_slots >= scheduled_slots) ? pe_compact_batch_read_slots - scheduled_slots : 0;
  const size_t pe_compact_batch_savings_slots =
      (read_slots >= pe_compact_batch_read_slots) ? read_slots - pe_compact_batch_read_slots : 0;
  const size_t lane_static_real_batch_savings_slots =
      (read_slots >= lane_static_real_batch_read_slots) ? read_slots - lane_static_real_batch_read_slots : 0;
  const size_t real_compact_batch_align_pad =
      (real_compact_batch_read_slots >= real_nnz) ? real_compact_batch_read_slots - real_nnz : 0;
  const size_t real_compact_batch_savings_slots =
      (read_slots >= real_compact_batch_read_slots) ? read_slots - real_compact_batch_read_slots : 0;
  const double density = read_slots ? (double)real_nnz / (double)read_slots : 0.0;
  const double channel_dynamic_density =
      channel_dynamic_read_slots ? (double)real_nnz / (double)channel_dynamic_read_slots : 0.0;
  const double pe_lower_density =
      scheduled_slots ? (double)real_nnz / (double)scheduled_slots : 0.0;
  const double pe_compact_batch_density =
      pe_compact_batch_read_slots ? (double)real_nnz / (double)pe_compact_batch_read_slots : 0.0;
  const double lane_static_real_batch_density =
      lane_static_real_batch_read_slots ? (double)real_nnz / (double)lane_static_real_batch_read_slots : 0.0;
  const double real_compact_batch_density =
      real_compact_batch_read_slots ? (double)real_nnz / (double)real_compact_batch_read_slots : 0.0;
  const double channel_dynamic_savings_pct =
      read_slots ? (double)channel_dynamic_savings_slots * 100.0 / (double)read_slots : 0.0;
  const double pe_dynamic_savings_pct =
      read_slots ? (double)pe_dynamic_total_savings_slots * 100.0 / (double)read_slots : 0.0;
  const double pe_compact_batch_savings_pct =
      read_slots ? (double)pe_compact_batch_savings_slots * 100.0 / (double)read_slots : 0.0;
  const double lane_static_real_batch_savings_pct =
      read_slots ? (double)lane_static_real_batch_savings_slots * 100.0 / (double)read_slots : 0.0;
  const double real_compact_batch_savings_pct =
      read_slots ? (double)real_compact_batch_savings_slots * 100.0 / (double)read_slots : 0.0;

  size_t pe_compact_stream_read_beats = 0;
  size_t lane_static_real_stream_read_beats = 0;
  size_t real_compact_stream_read_beats = 0;
  for (int c = 0; c < hbm; ++c) {
    channel_pe_compact_stream_beats[c] =
        ceil_div_size(channel_sched[c], (size_t)kPeNum);
    pe_compact_stream_read_beats += channel_pe_compact_stream_beats[c];
    size_t channel_lane_real_max = 0;
    for (int lane = 0; lane < kPeNum; ++lane) {
      const size_t pe = (size_t)c * (size_t)kPeNum + (size_t)lane;
      if (pe_real[pe] > channel_lane_real_max) {
        channel_lane_real_max = pe_real[pe];
      }
    }
    lane_static_real_stream_read_beats += channel_lane_real_max;
    real_compact_stream_read_beats += ceil_div_size(channel_real[c], (size_t)kPeNum);
  }
  const size_t pe_compact_stream_read_slots =
      pe_compact_stream_read_beats * (size_t)kPeNum;
  const size_t pe_compact_stream_align_pad =
      (pe_compact_stream_read_slots >= scheduled_slots) ? pe_compact_stream_read_slots - scheduled_slots : 0;
  const size_t pe_compact_stream_savings_slots =
      (read_slots >= pe_compact_stream_read_slots) ? read_slots - pe_compact_stream_read_slots : 0;
  const double pe_compact_stream_density =
      pe_compact_stream_read_slots ? (double)real_nnz / (double)pe_compact_stream_read_slots : 0.0;
  const double pe_compact_stream_savings_pct =
      read_slots ? (double)pe_compact_stream_savings_slots * 100.0 / (double)read_slots : 0.0;
  const size_t lane_static_real_stream_read_slots =
      lane_static_real_stream_read_beats * (size_t)kPeNum;
  const size_t lane_static_real_stream_savings_slots =
      (read_slots >= lane_static_real_stream_read_slots) ? read_slots - lane_static_real_stream_read_slots : 0;
  const double lane_static_real_stream_density =
      lane_static_real_stream_read_slots ? (double)real_nnz / (double)lane_static_real_stream_read_slots : 0.0;
  const double lane_static_real_stream_savings_pct =
      read_slots ? (double)lane_static_real_stream_savings_slots * 100.0 / (double)read_slots : 0.0;
  const size_t lane_static_real_hbm_beats_min =
      min_size_array(channel_lane_static_real_beats, hbm);
  const size_t lane_static_real_hbm_beats_max =
      max_size_array(channel_lane_static_real_beats, hbm);
  const size_t lane_static_real_hbm_ideal_beats =
      ceil_div_size(lane_static_real_batch_read_beats, (size_t)hbm);
  const size_t lane_static_real_hbm_extra_beats =
      (lane_static_real_hbm_beats_max > lane_static_real_hbm_ideal_beats)
          ? lane_static_real_hbm_beats_max - lane_static_real_hbm_ideal_beats
          : 0;
  const double lane_static_real_hbm_balance_eff =
      balance_efficiency(lane_static_real_batch_read_beats, hbm,
                         lane_static_real_hbm_beats_max);
  const size_t real_compact_stream_read_slots =
      real_compact_stream_read_beats * (size_t)kPeNum;
  const size_t real_compact_stream_align_pad =
      (real_compact_stream_read_slots >= real_nnz) ? real_compact_stream_read_slots - real_nnz : 0;
  const size_t real_compact_stream_savings_slots =
      (read_slots >= real_compact_stream_read_slots) ? read_slots - real_compact_stream_read_slots : 0;
  const double real_compact_stream_density =
      real_compact_stream_read_slots ? (double)real_nnz / (double)real_compact_stream_read_slots : 0.0;
  const double real_compact_stream_savings_pct =
      read_slots ? (double)real_compact_stream_savings_slots * 100.0 / (double)read_slots : 0.0;
  const size_t balanced_channel_target_nnz =
      ceil_div_size(real_nnz, (size_t)hbm);
  const size_t balanced_compact_stream_channel_beats =
      ceil_div_size(balanced_channel_target_nnz, (size_t)kPeNum);
  const size_t balanced_compact_stream_read_beats =
      balanced_compact_stream_channel_beats * (size_t)hbm;
  const size_t balanced_compact_stream_read_slots =
      balanced_compact_stream_read_beats * (size_t)kPeNum;
  const size_t balanced_compact_stream_savings_slots =
      (read_slots >= balanced_compact_stream_read_slots) ? read_slots - balanced_compact_stream_read_slots : 0;
  const double balanced_compact_stream_density =
      balanced_compact_stream_read_slots ? (double)real_nnz / (double)balanced_compact_stream_read_slots : 0.0;
  const double balanced_compact_stream_savings_pct =
      read_slots ? (double)balanced_compact_stream_savings_slots * 100.0 / (double)read_slots : 0.0;

  size_t channel_real_min = channel_real[0];
  size_t channel_real_max = channel_real[0];
  for (int i = 1; i < hbm; ++i) {
    if (channel_real[i] < channel_real_min) channel_real_min = channel_real[i];
    if (channel_real[i] > channel_real_max) channel_real_max = channel_real[i];
  }
  size_t pe_real_min = pe_real[0];
  size_t pe_real_max = pe_real[0];
  for (int i = 1; i < num_pe; ++i) {
    if (pe_real[i] < pe_real_min) pe_real_min = pe_real[i];
    if (pe_real[i] > pe_real_max) pe_real_max = pe_real[i];
  }
  size_t pe_sched_min = pe_sched[0];
  size_t pe_sched_max = pe_sched[0];
  for (int i = 1; i < num_pe; ++i) {
    if (pe_sched[i] < pe_sched_min) pe_sched_min = pe_sched[i];
    if (pe_sched[i] > pe_sched_max) pe_sched_max = pe_sched[i];
  }
  size_t channel_dynamic_len_min = channel_dynamic_len[0];
  size_t channel_dynamic_len_max = channel_dynamic_len[0];
  for (int i = 1; i < hbm; ++i) {
    if (channel_dynamic_len[i] < channel_dynamic_len_min) {
      channel_dynamic_len_min = channel_dynamic_len[i];
    }
    if (channel_dynamic_len[i] > channel_dynamic_len_max) {
      channel_dynamic_len_max = channel_dynamic_len[i];
    }
  }
  const size_t channel_dynamic_ideal_beats =
      ceil_div_size(channel_dynamic_read_beats, (size_t)hbm);
  const size_t channel_dynamic_extra_beats =
      (channel_dynamic_len_max > channel_dynamic_ideal_beats)
          ? channel_dynamic_len_max - channel_dynamic_ideal_beats
          : 0;
  size_t pe_compact_stream_channel_beats_min = channel_pe_compact_stream_beats[0];
  size_t pe_compact_stream_channel_beats_max = channel_pe_compact_stream_beats[0];
  for (int i = 1; i < hbm; ++i) {
    if (channel_pe_compact_stream_beats[i] < pe_compact_stream_channel_beats_min) {
      pe_compact_stream_channel_beats_min = channel_pe_compact_stream_beats[i];
    }
    if (channel_pe_compact_stream_beats[i] > pe_compact_stream_channel_beats_max) {
      pe_compact_stream_channel_beats_max = channel_pe_compact_stream_beats[i];
    }
  }
  const double channel_real_balance_eff =
      balance_efficiency(real_nnz, hbm, channel_real_max);
  const double channel_sched_balance_eff =
      balance_efficiency(scheduled_slots, hbm, channel_sched ? max_size_array(channel_sched, hbm) : 0);
  const double channel_dyn_balance_eff =
      balance_efficiency(channel_dynamic_read_beats, hbm, channel_dynamic_len_max);
  const double pe_real_balance_eff =
      balance_efficiency(real_nnz, num_pe, pe_real_max);
  const double pe_sched_balance_eff =
      balance_efficiency(scheduled_slots, num_pe, pe_sched_max);
  const double pe_compact_stream_hbm_balance_eff =
      balance_efficiency(pe_compact_stream_read_beats, hbm,
                         pe_compact_stream_channel_beats_max);

  if (opt->csv) {
    printf("%s,%d,%d,%d,%d,%d,%zu,%d,%d,%d,%d,%zu,%zu,%zu,%.6f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%zu,%zu,%zu,%.2f,%zu,%.6f,%zu,%zu,%zu,%zu,%.2f,%zu,%zu,%.6f,%zu,%zu,%.2f,%zu,%zu,%.6f,%zu,%zu,%.2f,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%zu,%.2f,%zu,%zu,%.6f,%zu,%.2f,%zu,%zu,%.6f,%zu,%zu,%.2f,%zu,%zu,%.6f,%zu,%zu,%.2f,%zu,%zu,%.6f,%zu,%.2f,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%zu,%zu,%zu,%.6f,%zu,%zu,%zu,%zu\n",
           dataset, hbm, meta->m, meta->n, opt->drop_diag, opt->window, real_nnz,
           slice_size, batch_size, slice_width, batch_num, matrix_len,
           read_beats, read_slots, density, padding_slots, reorder_holes,
           batch_pad, channel_real_min, channel_real_max, pe_real_min,
           pe_real_max, channel_dynamic_read_beats, channel_dynamic_read_slots,
           channel_dynamic_density, channel_dynamic_padding_slots,
           channel_dynamic_batch_pad, channel_dynamic_savings_slots,
           channel_dynamic_savings_pct, scheduled_slots, pe_lower_density,
           channel_dynamic_len_min, channel_dynamic_len_max,
           pe_dynamic_intra_hbm_savings_slots,
           pe_dynamic_total_savings_slots,
           pe_dynamic_savings_pct,
           pe_compact_batch_read_beats,
           pe_compact_batch_read_slots,
           pe_compact_batch_density,
           pe_compact_batch_align_pad,
           pe_compact_batch_savings_slots,
           pe_compact_batch_savings_pct,
           pe_compact_stream_read_beats,
           pe_compact_stream_read_slots,
           pe_compact_stream_density,
           pe_compact_stream_align_pad,
           pe_compact_stream_savings_slots,
           pe_compact_stream_savings_pct,
           pe_sched_min,
           pe_sched_max,
           pe_compact_stream_channel_beats_min,
           pe_compact_stream_channel_beats_max,
           lane_static_real_batch_read_beats,
           lane_static_real_batch_read_slots,
           lane_static_real_batch_density,
           lane_static_real_batch_savings_slots,
           lane_static_real_batch_savings_pct,
           lane_static_real_stream_read_beats,
           lane_static_real_stream_read_slots,
           lane_static_real_stream_density,
           lane_static_real_stream_savings_slots,
           lane_static_real_stream_savings_pct,
           real_compact_batch_read_beats,
           real_compact_batch_read_slots,
           real_compact_batch_density,
           real_compact_batch_align_pad,
           real_compact_batch_savings_slots,
           real_compact_batch_savings_pct,
           real_compact_stream_read_beats,
           real_compact_stream_read_slots,
           real_compact_stream_density,
           real_compact_stream_align_pad,
           real_compact_stream_savings_slots,
           real_compact_stream_savings_pct,
           balanced_compact_stream_read_beats,
           balanced_compact_stream_read_slots,
           balanced_compact_stream_density,
           balanced_compact_stream_savings_slots,
           balanced_compact_stream_savings_pct,
           balanced_compact_stream_channel_beats,
           channel_real_balance_eff,
           channel_sched_balance_eff,
           channel_dyn_balance_eff,
           pe_real_balance_eff,
           pe_sched_balance_eff,
           pe_compact_stream_hbm_balance_eff,
           channel_dynamic_ideal_beats,
           channel_dynamic_extra_beats,
           channel_dynamic_len_max,
           lane_static_real_hbm_balance_eff,
           lane_static_real_hbm_beats_min,
           lane_static_real_hbm_beats_max,
           lane_static_real_hbm_ideal_beats,
           lane_static_real_hbm_extra_beats);
  } else {
    printf("\nHBM=%d\n", hbm);
    printf("  slice_size=%d batch_size=%d slice_width=%d col_slices=%d batch_num=%d num_pe=%d window=%d\n",
           slice_size, batch_size, slice_width, num_col_slices, batch_num, num_pe,
           opt->window);
    printf("  input_nnz=%zu diag_removed=%zu profiled_nnz=%zu\n",
           meta->nnz, diag_removed, real_nnz);
    printf("  matrix_len=%zu read_beats=%zu read_slots=%zu density=%.2f%%\n",
           matrix_len, read_beats, read_slots, density * 100.0);
    printf("  padding_slots=%zu pad/read=%.2f%% reorder_holes=%zu batch_pad=%zu\n",
           padding_slots,
           read_slots ? (double)padding_slots * 100.0 / (double)read_slots : 0.0,
           reorder_holes,
           batch_pad);
    printf("  per-HBM dynamic: read_beats=%zu read_slots=%zu density=%.2f%% padding=%zu batch_pad=%zu saved=%zu (%.2f%%)\n",
           channel_dynamic_read_beats,
           channel_dynamic_read_slots,
           channel_dynamic_density * 100.0,
           channel_dynamic_padding_slots,
           channel_dynamic_batch_pad,
           channel_dynamic_savings_slots,
           channel_dynamic_savings_pct);
    printf("  per-PE lower bound: read_slots=%zu density=%.2f%% remaining_holes=%zu\n",
           scheduled_slots,
           pe_lower_density * 100.0,
           reorder_holes);
    printf("  per-PE dynamic: read_slots=%zu density=%.2f%% intra_hbm_saved=%zu total_saved=%zu (%.2f%%)\n",
           scheduled_slots,
           pe_lower_density * 100.0,
           pe_dynamic_intra_hbm_savings_slots,
           pe_dynamic_total_savings_slots,
           pe_dynamic_savings_pct);
    printf("  per-PE compact512/batch: read_beats=%zu read_slots=%zu density=%.2f%% align_pad=%zu saved=%zu (%.2f%%)\n",
           pe_compact_batch_read_beats,
           pe_compact_batch_read_slots,
           pe_compact_batch_density * 100.0,
           pe_compact_batch_align_pad,
           pe_compact_batch_savings_slots,
           pe_compact_batch_savings_pct);
    printf("  per-PE compact512/stream: read_beats=%zu read_slots=%zu density=%.2f%% align_pad=%zu saved=%zu (%.2f%%)\n",
           pe_compact_stream_read_beats,
           pe_compact_stream_read_slots,
           pe_compact_stream_density * 100.0,
           pe_compact_stream_align_pad,
           pe_compact_stream_savings_slots,
           pe_compact_stream_savings_pct);
    printf("  lane-static real/batch: read_beats=%zu read_slots=%zu density=%.2f%% saved=%zu (%.2f%%)\n",
           lane_static_real_batch_read_beats,
           lane_static_real_batch_read_slots,
           lane_static_real_batch_density * 100.0,
           lane_static_real_batch_savings_slots,
           lane_static_real_batch_savings_pct);
    printf("  lane-static real/stream: read_beats=%zu read_slots=%zu density=%.2f%% saved=%zu (%.2f%%)\n",
           lane_static_real_stream_read_beats,
           lane_static_real_stream_read_slots,
           lane_static_real_stream_density * 100.0,
           lane_static_real_stream_savings_slots,
           lane_static_real_stream_savings_pct);
    printf("  real compact512/batch: read_beats=%zu read_slots=%zu density=%.2f%% align_pad=%zu saved=%zu (%.2f%%)\n",
           real_compact_batch_read_beats,
           real_compact_batch_read_slots,
           real_compact_batch_density * 100.0,
           real_compact_batch_align_pad,
           real_compact_batch_savings_slots,
           real_compact_batch_savings_pct);
    printf("  real compact512/stream: read_beats=%zu read_slots=%zu density=%.2f%% align_pad=%zu saved=%zu (%.2f%%)\n",
           real_compact_stream_read_beats,
           real_compact_stream_read_slots,
           real_compact_stream_density * 100.0,
           real_compact_stream_align_pad,
           real_compact_stream_savings_slots,
           real_compact_stream_savings_pct);
    printf("  balanced compact512/stream: read_beats=%zu read_slots=%zu density=%.2f%% saved=%zu (%.2f%%) channel_beats=%zu\n",
           balanced_compact_stream_read_beats,
           balanced_compact_stream_read_slots,
           balanced_compact_stream_density * 100.0,
           balanced_compact_stream_savings_slots,
           balanced_compact_stream_savings_pct,
           balanced_compact_stream_channel_beats);
    print_min_max_avg("HBM real nnz", channel_real, hbm);
    print_min_max_avg("HBM scheduled slots", channel_sched, hbm);
    print_min_max_avg("HBM dynamic beats", channel_dynamic_len, hbm);
    print_min_max_avg("HBM compact512 beats", channel_pe_compact_stream_beats, hbm);
    print_min_max_avg("PE real nnz", pe_real, num_pe);
    print_min_max_avg("PE scheduled slots", pe_sched, num_pe);
    printf("  load balance:\n");
    print_parallel_balance("HBM real nnz", channel_real, hbm);
    print_parallel_balance("HBM scheduled slots", channel_sched, hbm);
    print_parallel_balance("HBM dynamic beats", channel_dynamic_len, hbm);
    print_parallel_balance("HBM compact512 beats", channel_pe_compact_stream_beats, hbm);
    print_parallel_balance("HBM lane-real beats", channel_lane_static_real_beats, hbm);
    print_parallel_balance("PE real nnz", pe_real, num_pe);
    print_parallel_balance("PE scheduled slots", pe_sched, num_pe);
    printf("    strip bottleneck       total_hbm_dyn_beats=%zu ideal_per_hbm=%zu bottleneck=%zu extra=%zu eff=%.2f%%\n",
           channel_dynamic_read_beats,
           channel_dynamic_ideal_beats,
           channel_dynamic_len_max,
           channel_dynamic_extra_beats,
           channel_dyn_balance_eff * 100.0);

    if (opt->top_batches > 0 && batch_num > 0) {
      BatchStat* sorted = (BatchStat*)xmalloc((size_t)batch_num * sizeof(BatchStat));
      memcpy(sorted, batch_stats, (size_t)batch_num * sizeof(BatchStat));
      qsort(sorted, (size_t)batch_num, sizeof(BatchStat), cmp_batch_padding);
      const int top = (opt->top_batches < batch_num) ? opt->top_batches : batch_num;
      printf("  worst batches by padding:\n");
      printf("    batch      real  scheduled  max_len  global_slots  hbm_dyn_slots  pe_pack_slots  lane_real_slots  real_pack_slots  global_den  hbm_dyn_den  pe_pack_den  lane_real_den  real_pack_den  hbm_saved  pe_saved  real_saved\n");
      for (int i = 0; i < top; ++i) {
        const BatchStat* b = &sorted[i];
        printf("    %5d %9zu %10zu %8zu %13zu %14zu %14zu %16zu %16zu %9.2f%% %10.2f%% %10.2f%% %13.2f%% %13.2f%% %9zu %9zu %11zu\n",
               b->id, b->real, b->scheduled, b->max_len, b->read_slots,
               b->channel_read_slots, b->pe_compact_read_slots,
               b->lane_static_real_slots,
               b->real_compact_read_slots,
               b->density * 100.0,
               b->channel_density * 100.0,
               b->pe_compact_density * 100.0,
               b->lane_static_real_density * 100.0,
               b->real_compact_density * 100.0,
               b->read_slots - b->channel_read_slots,
               b->read_slots - b->pe_compact_read_slots,
               b->read_slots - b->real_compact_read_slots);
      }
      free(sorted);

      sorted = (BatchStat*)xmalloc((size_t)batch_num * sizeof(BatchStat));
      memcpy(sorted, batch_stats, (size_t)batch_num * sizeof(BatchStat));
      qsort(sorted, (size_t)batch_num, sizeof(BatchStat), cmp_batch_hbm_imbalance);
      printf("  worst batches by HBM imbalance:\n");
      printf("    batch  hbm_dyn_beats  min  max  ideal  extra  eff     real  scheduled  hbm_dyn_slots\n");
      for (int i = 0; i < top; ++i) {
        const BatchStat* b = &sorted[i];
        printf("    %5d %14zu %4zu %4zu %6zu %6zu %6.2f%% %7zu %10zu %14zu\n",
               b->id,
               b->channel_len_sum,
               b->channel_len_min,
               b->channel_len_max,
               b->channel_len_ideal,
               b->channel_len_extra,
               b->channel_balance_eff * 100.0,
               b->real,
               b->scheduled,
               b->channel_read_slots);
      }
      free(sorted);
    }

    if (dump_slots) {
      printf("  packet dump: batch=%d first %d beats per HBM\n",
             opt->dump_batch, opt->dump_beats);
      for (int c = 0; c < hbm; ++c) {
        for (int beat = 0; beat < opt->dump_beats; ++beat) {
          printf("    hbm%02d beat%03d:", c, beat);
          for (int slot = 0; slot < kPeNum; ++slot) {
            const int pe = c * kPeNum + slot;
            const Slot* s = dump_slots + (size_t)pe * (size_t)opt->dump_beats +
                            (size_t)beat;
            print_slot(s, opt->dump_batch, slice_width, num_pe);
          }
          printf("\n");
        }
      }
    } else if (opt->dump_batch >= batch_num) {
      printf("  packet dump skipped: batch %d is outside [0, %d)\n",
             opt->dump_batch, batch_num);
    }
  }

  free(dump_slots);
  free(pe_sched);
  free(pe_real);
  free(channel_lane_static_real_beats);
  free(channel_pe_compact_stream_beats);
  free(channel_dynamic_len);
  free(channel_sched);
  free(channel_real);
  free(batch_stats);
  for (size_t i = 0; i < bucket_num; ++i) free(buckets[i].elems);
  free(buckets);
}

static void usage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s <csr_dir> [options]\n"
          "\n"
          "Options:\n"
          "  --drop-diag           profile Jacobi R = A - D packing\n"
          "  --hbm LIST            comma-separated HBM counts, default 16,24,32\n"
          "  --window N            accumulator scheduling window, default 10\n"
          "  --top-batches N       print N worst padding batches, default 8\n"
          "  --dump-batch N        dump packed slots for one batch\n"
          "  --dump-beats N        beats per HBM for dump, default 4\n"
          "  --csv                 print compact CSV rows\n"
          "  --help                show this message\n"
          "\n"
          "Example:\n"
          "  %s data/suitesparse/Schmid/csr/thermal2_n65536 --drop-diag\n",
          argv0, argv0);
}

static void parse_hbm_list(const char* s, Options* opt) {
  opt->hbm_count = 0;
  const char* p = s;
  while (*p) {
    while (*p == ',' || isspace((unsigned char)*p)) ++p;
    if (!*p) break;
    char* end = NULL;
    errno = 0;
    long v = strtol(p, &end, 10);
    if (errno != 0 || end == p) die("bad --hbm list");
    if (opt->hbm_count >= (int)(sizeof(opt->hbm) / sizeof(opt->hbm[0]))) {
      die("too many --hbm entries");
    }
    opt->hbm[opt->hbm_count++] = (int)v;
    p = end;
    while (*p && *p != ',') {
      if (!isspace((unsigned char)*p)) die("bad --hbm list separator");
      ++p;
    }
  }
  if (opt->hbm_count == 0) die("empty --hbm list");
}

static Options default_options(void) {
  Options opt;
  memset(&opt, 0, sizeof(opt));
  opt.hbm[0] = 16;
  opt.hbm[1] = 24;
  opt.hbm[2] = 32;
  opt.hbm_count = 3;
  opt.top_batches = 8;
  opt.dump_batch = -1;
  opt.dump_beats = 4;
  opt.window = kDefaultWindow;
  return opt;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  const char* csr_dir = NULL;
  Options opt = default_options();

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--drop-diag") == 0) {
      opt.drop_diag = 1;
    } else if (strcmp(argv[i], "--csv") == 0) {
      opt.csv = 1;
    } else if (strcmp(argv[i], "--hbm") == 0) {
      if (++i >= argc) die("--hbm needs a value");
      parse_hbm_list(argv[i], &opt);
    } else if (strcmp(argv[i], "--window") == 0) {
      if (++i >= argc || !parse_int(argv[i], &opt.window) || opt.window <= 0) {
        die("--window needs a positive integer");
      }
    } else if (strcmp(argv[i], "--top-batches") == 0) {
      if (++i >= argc || !parse_int(argv[i], &opt.top_batches)) {
        die("--top-batches needs an integer");
      }
    } else if (strcmp(argv[i], "--dump-batch") == 0) {
      if (++i >= argc || !parse_int(argv[i], &opt.dump_batch)) {
        die("--dump-batch needs an integer");
      }
    } else if (strcmp(argv[i], "--dump-beats") == 0) {
      if (++i >= argc || !parse_int(argv[i], &opt.dump_beats)) {
        die("--dump-beats needs an integer");
      }
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    } else if (!csr_dir) {
      csr_dir = argv[i];
    } else {
      die("only one csr_dir is supported");
    }
  }

  if (!csr_dir) die("missing csr_dir");
  if (opt.top_batches < 0) opt.top_batches = 0;
  if (opt.dump_beats < 0) opt.dump_beats = 0;

  const Meta meta = read_meta(csr_dir);
  int* row_ptr = read_int_file_exact(csr_dir, "row_ptr.txt", (size_t)meta.m + 1u);
  int* col_idx = read_int_file_exact(csr_dir, "col_idx.txt", meta.nnz);
  float* values = read_float_file_optional(csr_dir, "values.txt", meta.nnz);

  if ((size_t)row_ptr[meta.m] != meta.nnz) {
    fprintf(stderr, "pack_profile: warning: row_ptr[m]=%d meta.nnz=%zu\n",
            row_ptr[meta.m], meta.nnz);
  }

  if (opt.csv) {
    print_csv_header();
  } else {
    printf("dataset=%s path=%s m=%d n=%d nnz=%zu drop_diag=%d\n",
           meta.name, csr_dir, meta.m, meta.n, meta.nnz, opt.drop_diag);
    if (!values) {
      printf("values.txt not found; packet dump uses value=1.0 for packed bits\n");
    }
  }

  for (int i = 0; i < opt.hbm_count; ++i) {
    profile_one_hbm(&meta, row_ptr, col_idx, values, &opt, opt.hbm[i], meta.name);
  }

  free(values);
  free(col_idx);
  free(row_ptr);
  return 0;
}
