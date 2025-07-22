# CFS

This is (probably) a filesystem. `main.cpp` is an API for loading and interacting with the filesystem.

CFS... :
- Is made of blocks
- Has errors (not yet actually)
- Is (probably) simple to understand
- Is **not** finished
- Is very unstable 👍
- Is mostly un-repairable in the case of corruption

## Concepts
Each filesystem (stored in a `CFS` struct) has a few things:
- `unsigned char bat[BAT_SIZE]`: A Block Allocation Table (BAT) which is a bitmap of all allocated blocks
- `FSBlock blocks[MAX_BLOCKS]`: A list of `FSBlock`s 
- `fsptr root`: An `fsptr` to the root folder

An `fsptr` is just an unsigned integer which represents the index of `cfs.blocks`

There are four types of blocks, which are `union`ed into `FSBlock`:
- `FileHeader`:
    ```c++
    char name[MAX_FILENAME];
	fsptr parent; // block ptr to parent DirectoryHeader
	fsptr next; // block ptr to next FileHeader
	unsigned int chunks;
	fsptr start; // block ptr to the first FileChunkHeader
    ```
    - File content is stored in chunks, which have two parts: the `FileChunkHeader` and the `FileChunkData`, which are explained later. 
    - The chunks are stored as a linked list of `FileChunkHeader`s, which point to their respective `FileChunkData`
- `DirectoryHeader`:
    ```c++
    char name[MAX_FILENAME];
	fsptr parent; // block ptr to parent DirectoryHeader
	fsptr next; // block ptr to next DirectoryHeader
	unsigned int files;
	fsptr first_file; // The start of the linked-list of files
	unsigned int directories;
	fsptr first_directory; // The start of the linked-list of sub-directories
    ```
- `FileChunkHeader`:
    ```c++
    unsigned int size;
	fsptr next; // block ptr to next block
	fsptr data; // block ptr to FileChunkData
    ```
- `FileChunkData`:
    ```c++
    unsigned char data[CHUNK_SIZE];
    ```

As you can see, chunks don't have any identifying information. While this might be changed later, for now you cannot tell what type of block something is just by looking at it, which makes the filesystem mostly unrepairable.

If I was going to add some identifying information, I would probably replace the BAT with a table that says what type of block each one is, but I'm too lazy to do that.

## API
- `void cfsLogBAT(CFS cfs)`
    - Logs the block allocation table
    - Ex. if blocks 2, 3, and 5 are used, it will print: `00101100` (in little-endian)
- `fsptr cfsAlloc(CFS* cfs, int* err)`
    - Tries to allocate a block. Returns an `fsptr` to the block if it worked, and sets `err` to `ERR_ALLOC` if it didn't
- `void cfsFree(CFS* cfs, fsptr ptr, int* err)`
    - Frees the block at `ptr`
    - Currently doesn't actually set `err`
- `void cfsZeroBlock(CFS* cfs, fsptr ptr)`
    - Zero's out the block at `ptr`
    - Literally just `memset(&(cfs->blocks[ptr]), 0, sizeof(FSBlock));`
- `CFS cfsInit()`
    - Returns a newly initialized `CFS`
- `fsptr cfsCreateFile(CFS* cfs, fsptr parent_ptr, char* fname, int* err)`
    - Allocates and sets up a new file under the `parent_ptr` directory with one empty chunk.
- `fsptr cfsCreateDir(CFS* cfs, fsptr parent_ptr, char* fname, int* err)`
    - Allocates and sets up a new directory under the `parent_ptr` directory
- `void cfsRemoveFile(CFS* cfs, fsptr idx, int* err)`
    - Removes a file, deallocating its header and its chunks
- `void cfsRemoveDir(CFS* cfs, fsptr idx, int* err)`
    - Removes a directory and its children (recursively)
- `void cfsPrintFile(CFS* cfs, fsptr file)`
    - Prints file information and contents
        - File name
        - \# of chunks
        - Each of the chunks with:
            - Chunk index
            - Header `fsptr`
            - Next header `fsptr`
            - Size
            - An `xxd`-like hex display
- `void cfsWriteFile(CFS* cfs, fsptr file, char* text, int* err)`
    - *Appends* `text` to the file at `file`
- `int cfsReadFile(CFS* cfs, fsptr file, char** text)`
    - Reads text from `file` into the string pointed to by `text` (it allocates a new string btw. This might be bad practice but idc this isn't supposed to be professional)
    - Returns the size of `text`
- `ListDirResults cfsListDir(CFS* cfs, DirectoryHeader dir)`
    - Returns the files and directories in `dir`
- `fsptr cfsPath2Ptr(CFS* cfs, char* path, fsptr base, int* err)`
    - Returns the `fsptr` pointed to by `path` (relative to `base`)
    - A `..` in the path will go back one directory
    - Ex. `cfsPath2Ptr(&cfs, "F1/F2/../X", cfs.root, &err)` will return the `fsptr` to `X` (given that the absolute path to `X` is `/F1/X` and that `F1/` contains `F2/`)
- `void cfsTree(CFS* cfs, fsptr ptr, int level)`
    - Prints a file tree of the directory at `ptr`
- `void cfsSave(CFS* cfs, char* fname, int* err)`
    - Saves the filesystem to the *real* file at `fname`
- `CFS cfsLoad(char* fname, int* err)`
    - Loads a filesystem from the *real* file at `fname`


~~Ok I'm tired I'll finish this documentation later (no he won't)~~
I did it! (almost)

## Todo

- Finish error handling (some functions don't actually set `err` yet)
- Add `cfsClearFile()`
- Make a fancy filesystem data explorer to debug / see low level details
- Actually use this in a project
- Maybe make this just a C file and not C++ bc it doesn't use many C++ features
- Docs:
    - Add examples (which are kinda already in `main.cpp`)
