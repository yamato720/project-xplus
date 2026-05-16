# SuiteSparse Schmid Datasets

Source: SuiteSparse Matrix Collection, `Schmid/thermal2`.

Directory layout:

- `raw/`: downloaded `.tar.gz` archive.
- `mtx/`: extracted MatrixMarket files.
- `csr/`: Project-XPlus CSR text format: `row_ptr.txt`, `col_idx.txt`, `values.txt`, `b.txt`, `x0.txt`.

Datasets:

| Name | n | nnz in CSR | Notes |
|---|---:|---:|---|
| `thermal2` | 1,228,045 | 8,580,313 | Full million-row symmetric positive definite thermal problem, uses packaged `thermal2_b.mtx`. |
| `thermal2_n1024` | 1,024 | 6,362 | Leading principal submatrix used by the current `kMaxN = 1024` bitstream smoke test. |

The full `thermal2` dataset is staged for later large-problem support. The current hardware path still enforces `kMaxN = 1024`.

See `../SOURCES.md` for source URLs, checksums, and conversion notes.
