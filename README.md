# CFS

This is (probably) a filesystem. `main.cpp` is an API for loading and interacting with the filesystem.

CFS... :
- Is made of blocks
- Has errors (cool!)
- Is (probably) simple to understand
- Is **not** finished
- Is very unstable
- Is mostly un-repairable in the case of corruption

## API
- `void cfsLogBAT(CFS cfs)`
    - Logs the block allocation table
    - Ex. if blocks 2,3, and 5 are used, it will print: `01101000`
- `fsptr cfsAlloc(CFS* cfs, int* err)`
    - Tries to allocate a block. Returns an `fsptr` to the block if it worked, and sets `err` to `ERR_ALLOC` if it didn't
- `void cfsFree(CFS* cfs, fsptr ptr, int* err)`
    - Frees the block at `ptr`
    - Currently doesn't actually set `err`
- `CFS cfsInit()`
    - Returns a newly initialized `CFS`

Ok I'm tired I'll finish this documentation later (no he won't)
