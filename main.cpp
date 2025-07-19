#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define LOGd(expr) printf(#expr " = %d\n", (expr))
#define LOGs(expr) printf(#expr " = %s\n", (expr))

#define MAX_FILENAME 128
#define CHUNK_SIZE 256
#define MAX_BLOCKS 32 // change this to something higher for a usable experience
#define BAT_SIZE ((MAX_BLOCKS+1)/8) // adding 1 to MAX_BLOCKS will make it always round up when dividing

#define ERR_NONE 0
#define ERR_ALLOC 1
#define ERR_FILENAME 2



typedef unsigned int fsptr; // just an index for CFS.chunks

struct FileChunkData {
	unsigned char data[CHUNK_SIZE];
};

struct FileChunkHeader {
	unsigned int size;
	fsptr next; // block ptr to next block
	fsptr data; // block ptr to FileChunkData
};

struct FileHeader {
	char name[MAX_FILENAME];
	fsptr parent; // block ptr to parent DirectoryHeader
	fsptr next; // block ptr to next FileHeader
	unsigned int chunks;
	fsptr start; // block ptr to the first FileChunkHeader
};

struct DirectoryHeader {
	char name[MAX_FILENAME];
	fsptr parent;
	fsptr next; // block ptr to next DirectoryHeader
	unsigned int files;
	fsptr first_file;
	unsigned int directories;
	fsptr first_directory;
};

union FSBlock {
	DirectoryHeader dir;
	FileHeader file;
	FileChunkHeader file_ch_head;
	FileChunkData file_ch_data;
};

struct CFS {
	char magic[4] = "cfs";
	fsptr root; // block ptr to root DirectoryHeader
	unsigned char bat[BAT_SIZE]; // Block Allocation Table, its a bitmap
	FSBlock blocks[MAX_BLOCKS];
};



void print_bytes(void *ptr, size_t size) {
    unsigned char *c = (unsigned char*)ptr;
    while (size--) {
        printf("%02x ", *c++);
    }
	printf("\n");
}



void cfsLogBAT(CFS cfs) {
	int sum=0;
	for (int i=0;i<BAT_SIZE;i++) {
		for (int j=0;j<8;j++) {
			int v = (cfs.bat[i]<<j & (0b10000000))>>7;
			printf("%d",v);
			sum+=v;
		}
		printf(" ");
	}
	printf(" (total:%d)",sum);
	printf("\n");
}

// allocate a block, return the block index
fsptr cfsAlloc(CFS* cfs, int* err) {
	// printf("allocating\n");
	// find a block
	for (int i=0;i<BAT_SIZE;i++) {
		// printf("i=%d\n",i);
		unsigned char c = cfs->bat[i];
		for (int j=0;j<8;j++) {
			// printf("  j=%d\n",j);
			int v = (c>>j)&1;
			// printf("    v=%d\n",v);
			// printf("j=%d\n",j);
			if (v == 0) {
				// i found one
				// printf("    found one!\n");
				cfs->bat[i] |= 1<<j;
				*err = ERR_NONE;
				return i*8 + j;
			}
		}
	}
	*err = ERR_ALLOC;
	return 0;
}

void cfsFree(CFS* cfs, fsptr ptr, int* err) {
	int i = ptr/8;
	int j = ptr%8;
	cfs->bat[i] &= ~(1<<j);
}

CFS cfsInit() {
	CFS cfs;
	memset(&cfs,0,sizeof(CFS));
	// for (int i=0;i<MAX_BLOCKS;i++) {
	// 	cfs.bat[i]=0;
	// 	msm
	// }
	cfs.root=0;

	int err = 0;

	cfs.root = cfsAlloc(&cfs,&err); // dont check for errors bc the BAT is empty
	
	DirectoryHeader root = (DirectoryHeader){
		.name = "root",
		.next = 0, // lets hope this doesn't cause problems :)
		.files = 0,
		.first_file = 0, // same with this
		.directories = 0,
		.first_directory = 0, // and this
	};
	cfs.blocks[cfs.root] = (FSBlock){.dir = root};

	return cfs;
}

fsptr cfsCreateFile(CFS* cfs, fsptr parent_ptr, char* fname, int* err) {
	// make sure fname is okay
	if (strlen(fname) >= MAX_FILENAME) {
		*err = ERR_FILENAME;
		return 0;
	}

	DirectoryHeader* parent = &cfs->blocks[parent_ptr].dir;
	
	// allocate blocks
	int err2=0;
	fsptr fh_idx = cfsAlloc(cfs,&err2);
	if (err2) { *err = err2; return 0; }
	fsptr fch_idx = cfsAlloc(cfs,&err2);
	if (err2) { *err = err2; return 0; }
	fsptr fcd_idx = cfsAlloc(cfs,&err2);
	if (err2) { *err = err2; return 0; }

	// generate block data
	FileChunkData ch_data;
	for (int i=0;i<CHUNK_SIZE;i++)
		ch_data.data[i]=0;
	
	FileChunkHeader ch_head = {
		.size = 0,
		.next = 0,
		.data = fcd_idx,
	};

	FileHeader f_head = {
		.name = "", // we'll set this later
		.parent = parent_ptr,
		.next = 0,
		.chunks = 1,
		.start = fch_idx,
	};

	// now copy the name over
	for (int i=0;i<strlen(fname)+1;i++)
		f_head.name[i]=fname[i];

	if (parent->files > 0) { // if this isn't the first file, make sure that the current first file is the next in line bc we're inserting this one to the front
		f_head.next=parent->first_file;
	}

	// write blocks
	cfs->blocks[fcd_idx] = (FSBlock){.file_ch_data = ch_data};
	cfs->blocks[fch_idx] = (FSBlock){.file_ch_head = ch_head};
	cfs->blocks[fh_idx] = (FSBlock){.file = f_head};

	// add this file to the parent dir
	parent->files++;
	parent->first_file = fh_idx;

	return fh_idx;
}

fsptr cfsCreateDir(CFS* cfs, fsptr parent_ptr, char* fname, int* err) {
	// make sure fname is okay
	if (strlen(fname) >= MAX_FILENAME) {
		*err = ERR_FILENAME;
		return 0;
	}
	
	DirectoryHeader* parent = &cfs->blocks[parent_ptr].dir;

	// allocate blocks
	int err2=0;
	fsptr idx = cfsAlloc(cfs,&err2);
	if (err2) { *err = err2; return 0; }

	// generate block data
	DirectoryHeader head = {
		.name = "", // we'll set this later
		.parent = parent_ptr,
		.next = 0,
		.files = 0,
		.first_file = 0,
		.directories = 0,
		.first_directory = 0,
	};

	// now copy the name over
	for (int i=0;i<strlen(fname)+1;i++)
		head.name[i]=fname[i];

	if (parent->directories > 0) { // if this isn't the first file, make sure that the current first file is the next in line bc we're inserting this one to the front
		head.next=parent->first_directory;
	}

	// write blocks
	cfs->blocks[idx] = (FSBlock){.dir = head};

	// add this file to the parent dir
	parent->directories++;
	parent->first_directory = idx;

	return idx;
}

#define PTR2FILE(ptr) cfs->blocks[ptr].file
void cfsRemoveFile(CFS* cfs, fsptr idx, int* err) {
	FileHeader head = cfs->blocks[idx].file;
	DirectoryHeader* parent = &cfs->blocks[head.parent].dir;

	// printf("deleting file %d ");
	// LOGs(head.name);

	// remove from linked list
	// if its the first one we do something special
	if (strcmp(PTR2FILE(parent->first_file).name, head.name) == 0) {
		// the file is the first one!
		// LOGd(head.next);
		// LOGs(PTR2FILE(head.next).name);
		parent->first_file = head.next;
		// printf("its the first one\n");
	} else {
		// its not the first one :(  so we search for it

		// LOGs(PTR2FILE(parent->first_file).name);
		fsptr lastptr = parent->first_file;
		fsptr ptr = PTR2FILE(lastptr).next;
		for (int i=0;i<parent->files;i++) {
			// LOGs(PTR2FILE(lastptr).name);
			// LOGs(PTR2FILE(ptr).name);
			if (strcmp(PTR2FILE(ptr).name, head.name) == 0) {
				// LOGd(i);
				PTR2FILE(lastptr).next = head.next;
				break;
			}
			lastptr = ptr;
			ptr = PTR2FILE(ptr).next;
		}
	}

	parent->files--;

	// deallocate
	fsptr current_chunk = head.start;
	int err2 = 0;
	// printf("  freeing %d (chunk 0 data)\n",idx);
	cfsFree(cfs,cfs->blocks[current_chunk].file_ch_head.data,&err2);
	// printf("  freeing %d (chunk 0 head)\n",idx);
	cfsFree(cfs,current_chunk,&err2);
	for (int i=1;i<head.chunks;i++) {
		current_chunk = cfs->blocks[current_chunk].file_ch_head.next;
		// printf("  freeing %d (chunk %d data)\n",idx,i);
		cfsFree(cfs,cfs->blocks[current_chunk].file_ch_head.data,&err2);
		// printf("  freeing %d (chunk %d head)\n",idx,i);
		cfsFree(cfs,current_chunk,&err2);
	}
	// printf("  freeing %d (self)\n",idx);
	cfsFree(cfs,idx,&err2);
}

#define PTR2DIR(ptr) cfs->blocks[ptr].dir
void cfsRemoveDir(CFS* cfs, fsptr idx, int* err) {
	DirectoryHeader head = cfs->blocks[idx].dir;
	DirectoryHeader* parent = &cfs->blocks[head.parent].dir;

	// printf("deleting dir %d ",idx);
	// LOGs(head.name);

	// remove subdirectories
	// we have to gather a list of what to remove before removing them bc we don't know the `next` property if they're deleted right away
	{
		fsptr* to_remove = (fsptr*)calloc(head.directories,sizeof(fsptr));
		
		fsptr d = head.first_directory;
		to_remove[0]=d;
		for (int i=1;i<head.directories;i++) {
			d = PTR2DIR(d).next;
			to_remove[i] = d;
		}
		
		int err2=0; // TODO: actually detect errors
		for (int i=0;i<head.directories;i++) {
			cfsRemoveDir(cfs, to_remove[i], &err2);
		}
	}
	

	// remove subfiles
	// we have to gather a list of what to remove before removing them bc we don't know the `next` property if they're deleted right away
	{
		fsptr* to_remove = (fsptr*)calloc(head.files,sizeof(fsptr));
		
		fsptr d = head.first_file;
		to_remove[0]=d;
		for (int i=1;i<head.files;i++) {
			d = PTR2FILE(d).next;
			to_remove[i] = d;
		}
		
		int err2=0; // TODO: actually detect errors
		for (int i=0;i<head.files;i++) {
			cfsRemoveFile(cfs, to_remove[i], &err2);
		}
	}

	// remove from linked list
	if (strcmp(PTR2DIR(parent->first_directory).name, head.name) == 0) {
		// the file is the first one!
		// LOGd(head.next);
		// LOGs(PTR2DIR(head.next).name);
		parent->first_directory = head.next;
		// printf("its the first one\n");
	} else {
		// LOGs(PTR2FILE(parent->first_file).name);
		fsptr lastptr = parent->first_directory;
		fsptr ptr = PTR2DIR(lastptr).next;
		for (int i=0;i<parent->directories;i++) {
			// LOGs(PTR2FILE(lastptr).name);
			// LOGs(PTR2FILE(ptr).name);
			if (strcmp(PTR2DIR(ptr).name, head.name) == 0) {
				// LOGd(i);
				PTR2DIR(lastptr).next = head.next;
				break;
			}
			lastptr = ptr;
			ptr = PTR2DIR(ptr).next;
		}
	}

	parent->directories--;

	// deallocate
	int err2=0;
	// printf("freeing myself dir (%d)\n",idx);
	cfsFree(cfs,idx,&err2);
}

struct ListDirResults {
	int file_count;
	FileHeader* files;
	fsptr* file_ptrs;
	int dir_count;
	DirectoryHeader* dirs;
	fsptr* dir_ptrs;
};

ListDirResults cfsListDir(CFS* cfs, DirectoryHeader dir) {
	ListDirResults res;
	res.file_count = dir.files;
	res.dir_count = dir.directories;
	res.files = (FileHeader*) calloc(res.file_count, sizeof(FileHeader));
	res.dirs = (DirectoryHeader*) calloc(res.dir_count, sizeof(DirectoryHeader));
	res.file_ptrs = (fsptr*) calloc(res.file_count, sizeof(fsptr));
	res.dir_ptrs = (fsptr*) calloc(res.dir_count, sizeof(fsptr));
	
	if (res.file_count>0) {
		// FileHeader f = cfs->blocks[dir.first_file].file;
		fsptr f = dir.first_file;
		res.files[0] = PTR2FILE(f);
		res.file_ptrs[0] = f;
		for (int i=1;i<res.file_count;i++) {
			f = PTR2FILE(f).next;
			res.files[i] = PTR2FILE(f);
			res.file_ptrs[i] = f;
		}
	}

	if (res.dir_count>0) {
		// DirectoryHeader d = cfs->blocks[dir.first_directory].dir;
		fsptr d = dir.first_directory;
		res.dirs[0] = PTR2DIR(d);
		res.dir_ptrs[0] = d;
		for (int i=1;i<res.dir_count;i++) {
			d = PTR2DIR(d).next;
			res.dirs[i] = PTR2DIR(d);
			res.dir_ptrs[i] = d;
		}
	}

	return res;
}

void cfsTree(CFS* cfs, fsptr idx, int level) {
	char* indent = (char*)malloc(2*level);
	for (int i=0;i<level*2;i++)
	indent[i]=' ';
	indent[level*2]='\0';

	DirectoryHeader dir = PTR2DIR(idx);
	
	printf("%sdir: '%s' (%d -> %d)\n", indent, dir.name, idx, dir.next);
	ListDirResults res = cfsListDir(cfs,dir);
	for (int i=0;i<res.dir_count;i++) {
		cfsTree(cfs,res.dir_ptrs[i],level+1);
	}
	for (int i=0;i<res.file_count;i++) {
		printf("%s  file: '%s' (%d -> %d)\n", indent, res.files[i].name, res.file_ptrs[i], res.files[i].next);
	}
}

void cfsSave(CFS* cfs, char* fname, int* err) {
	FILE* f=fopen(fname,"w");
	
	if (f == NULL) {
		perror("error opening file for writing");
		*err = 100;
		return;
	}

	size_t written = fwrite(cfs, sizeof(CFS), 1, f);
    if (written != 1) {
        perror("Error writing to file");
        fclose(f);
        return;
    }

	fclose(f);
}
CFS cfsLoad(char* fname, int* err) {
	FILE* f=fopen(fname,"r");
	
	if (f == NULL) {
		perror("error opening file for reading");
		*err = 100;
		return {};
	}

	CFS fs;
	fread(&fs,sizeof(CFS),1,f);
	
	fclose(f);

	return fs;
}


int main() {
	printf("helo\n");
	// LOGd(sizeof(FileChunkHeader));
	// LOGd(sizeof(FileChunkData));
	// LOGd(sizeof(FileHeader));
	// LOGd(sizeof(DirectoryHeader));
	// LOGd(sizeof(FSBlock));
	// LOGd(sizeof(CFS));
	// printf("CFS = %fkb\n", (float)sizeof(CFS)/1000.f);
	// printf("CFS = %fkib\n", (float)sizeof(CFS)/1024.f);

	int err = 0;
	// CFS cfs = cfsInit();
	CFS cfs = cfsLoad("fs",&err);
	// cfsLogBAT(cfs);
	
	// fsptr cool_dir   = cfsCreateDir (&cfs, cfs.root, "F1", &err);
	// fsptr cool_file4 = cfsCreateFile(&cfs, cool_dir, "X", &err);
	// fsptr cool_file5 = cfsCreateFile(&cfs, cool_dir, "Y", &err);
	// fsptr cool_dir2  = cfsCreateDir (&cfs, cool_dir, "F2", &err);
	// fsptr cool_file6 = cfsCreateFile(&cfs, cool_dir2, "Z", &err);
	// fsptr cool_file  = cfsCreateFile(&cfs, cfs.root, "A", &err);
	// fsptr cool_file2 = cfsCreateFile(&cfs, cfs.root, "B", &err);
	// fsptr cool_file3 = cfsCreateFile(&cfs, cfs.root, "C", &err);

	// if (err) {
	// 	printf("error %d :(\n",err);
	// }
	
	cfsLogBAT(cfs);

	printf("Tree:\n");
	cfsTree(&cfs,cfs.root,0);


	cfsSave(&cfs,"fs2",&err);

	return 0;
}
