# SuiteSparse NASA Datasets

Source: SuiteSparse Matrix Collection, `Nasa/*`.

Directory layout:

- `raw/`: downloaded `.tar.gz` archives.
- `mtx/`: extracted MatrixMarket files.
- `csr/`: Project-XPlus CSR text format: `row_ptr.txt`, `col_idx.txt`, `values.txt`, `b.txt`, `x0.txt`.

Datasets:

| Name | n | nnz in CSR | Notes |
|---|---:|---:|---|
| `nasa2910` | 2,910 | 174,296 | SPD structural problem, has SuiteSparse `b` vector. |
| `nasa4704` | 4,704 | 104,756 | SPD structural problem, has SuiteSparse `b` vector. |
| `nasasrb` | 54,870 | 2,677,324 | Larger NASA shuttle rocket booster SPD case, has SuiteSparse `b` vector. |
| `pwt` | 36,519 | 326,107 | NASA structural matrix, generated `b = A * x_ref`; verify suitability before PCG use. |

Current Project-XPlus kernels define `kMaxN = 1024` in `include/cg_common.hpp`, so these datasets are staged for data integration work and will not run through the current host/kernel path without increasing the supported problem size.

See `../SOURCES.md` for source URLs, checksums, and conversion notes.
