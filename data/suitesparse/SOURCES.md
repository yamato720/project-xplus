# SuiteSparse Dataset Sources

All matrices in this directory were downloaded from the SuiteSparse Matrix Collection in MatrixMarket format and converted to Project-XPlus CSR text files.

Conversion output format:

- `row_ptr.txt`: CSR row pointer, zero-based.
- `col_idx.txt`: CSR column indices, zero-based.
- `values.txt`: matrix values, double precision text.
- `b.txt`: right-hand side vector.
- `x0.txt`: initial guess, currently all zeros.
- `meta.txt` / `meta.json`: local metadata.

## Dataset Inventory

| Case | Official Page | Download URL | Local CSR Path | n | CSR nnz | Notes |
|---|---|---|---|---:|---:|---|
| `Nasa/nasa2910` | https://sparse.tamu.edu/Nasa/nasa2910 | https://sparse.tamu.edu/MM/Nasa/nasa2910.tar.gz | `data/suitesparse/Nasa/csr/nasa2910` | 2,910 | 174,296 | NASA structural SPD problem; uses packaged `nasa2910_b.mtx`. |
| `Nasa/nasa4704` | https://sparse.tamu.edu/Nasa/nasa4704 | https://sparse.tamu.edu/MM/Nasa/nasa4704.tar.gz | `data/suitesparse/Nasa/csr/nasa4704` | 4,704 | 104,756 | NASA structural SPD problem; uses packaged `nasa4704_b.mtx`. |
| `Nasa/nasasrb` | https://sparse.tamu.edu/Nasa/nasasrb | https://sparse.tamu.edu/MM/Nasa/nasasrb.tar.gz | `data/suitesparse/Nasa/csr/nasasrb` | 54,870 | 2,677,324 | Larger NASA shuttle rocket booster SPD problem; uses packaged `nasasrb_b.mtx`. |
| `Nasa/pwt` | https://sparse.tamu.edu/Nasa/pwt | https://sparse.tamu.edu/MM/Nasa/pwt.tar.gz | `data/suitesparse/Nasa/csr/pwt` | 36,519 | 326,107 | NASA structural matrix; `b` generated locally as `A * x_ref`; verify PCG suitability before use. |
| `Schmid/thermal2` | https://sparse.tamu.edu/Schmid/thermal2 | https://sparse.tamu.edu/MM/Schmid/thermal2.tar.gz | `data/suitesparse/Schmid/csr/thermal2` | 1,228,045 | 8,580,313 | Full million-row symmetric positive definite thermal problem; uses packaged `thermal2_b.mtx`. |
| `Schmid/thermal2_n1024` | derived from `Schmid/thermal2` | derived locally | `data/suitesparse/Schmid/csr/thermal2_n1024` | 1,024 | 6,362 | Leading 1024x1024 principal submatrix for the current `kMaxN = 1024` bitstream smoke test; `b` generated locally as `A_sub * x_ref`. |

Any `Schmid/thermal2_n<N>` dataset can be generated locally with:

```bash
make download-suitesparse-data DATASETS=thermal2_n<N>
```

This creates the leading `N x N` principal submatrix of full `thermal2` and writes it to `data/suitesparse/Schmid/csr/thermal2_n<N>`.
For an SPD matrix, every principal submatrix is also SPD, so these derived cases remain mathematically valid PCG inputs.
Practical convergence still depends on conditioning, floating-point behavior, tolerance, `max_iters`, and the current hardware size limit.

## Raw Archive Checksums

| Archive | SHA256 |
|---|---|
| `data/suitesparse/Nasa/raw/nasa2910.tar.gz` | `457b4d58b008f691e2c3541225f217c145f9c1495394d2ca048e364c3dd4672b` |
| `data/suitesparse/Nasa/raw/nasa4704.tar.gz` | `f4170dd3d10a7ae027c17373cb09d8bf44e674f11ba2455b7272c22c93f5b7a4` |
| `data/suitesparse/Nasa/raw/nasasrb.tar.gz` | `ca86c95c0b8ed085e0251b3b162cb43b6b3b006bde58e37b54dba135a2dd8c9c` |
| `data/suitesparse/Nasa/raw/pwt.tar.gz` | `9a1419820b3696743ef6d05856b505d62d6859bc08465fe0aea4aaa6d58ec7ca` |
| `data/suitesparse/Schmid/raw/thermal2.tar.gz` | `02934a4b642b6829c33517e0b801b60ea894a6552c6cd7e3db6c709c776434ce` |

## Current Runtime Limit

The current kernel code sets `kMaxN = 1024` in `include/cg_common.hpp`. This is a code and bitstream design limit, not a dataset limit. The full `thermal2` dataset is staged on disk, but it cannot run on the current xclbin until the kernels are redesigned for larger vectors and the hardware bitstream is rebuilt.

The current hardware smoke test therefore uses `Schmid/thermal2_n1024`.
