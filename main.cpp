#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>


#define LOGd(expr) printf(#expr " = %d\n", (expr))
#define LOGs(expr) printf(#expr " = %s\n", (expr))

#define MAX_FILENAME 128
#define CHUNK_SIZE 128
#define MAX_BLOCKS 32 // change this to something higher for a usable experience
#define BAT_SIZE ((MAX_BLOCKS+1)/8) // adding 1 to MAX_BLOCKS will make it always round up when dividing

#define ERR_NONE 0
#define ERR_ALLOC 1
#define ERR_FILENAME 2

#define PTR2FILE(ptr) cfs->blocks[ptr].file
#define PTR2DIR(ptr) cfs->blocks[ptr].dir
#define PTR2FHEAD(ptr) cfs->blocks[ptr].file_ch_head
#define PTR2FDATA(ptr) cfs->blocks[ptr].file_ch_data


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

struct ListDirResults {
	int file_count;
	FileHeader* files;
	fsptr* file_ptrs;
	int dir_count;
	DirectoryHeader* dirs;
	fsptr* dir_ptrs;
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

void cfsZeroBlock(CFS* cfs, fsptr ptr) {
	memset(&cfs->blocks[ptr],0,sizeof(FSBlock));
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

void cfsPrintFile(CFS* cfs, fsptr file) {
	FileHeader fh = PTR2FILE(file);
	printf("File '%s':\n",fh.name);
	printf("chunks=%d\n",fh.chunks);
	fsptr chunk_ptr = fh.start;
	for (int i=0;i<fh.chunks;i++) {
		FileChunkHeader ch = cfs->blocks[chunk_ptr].file_ch_head;
		printf("  chunk %d ptr=%d next=%d size=%d\n",i,chunk_ptr,ch.next,ch.size);
		for (int j=0;j<ch.size/8+1;j++) {
			int start = j*8;
			int end = j*8+8;
			int extra = 0;
			if (end > ch.size) {
				extra = end-ch.size;
				end = ch.size;
			}
			for (int k=start;k<end;k++)
				printf("%02x ",cfs->blocks[ch.data].file_ch_data.data[k]);
			for (int k=0;k<extra;k++)
				printf("   ");
			printf(" | ");
			for (int k=start;k<end;k++) {
				unsigned char v = cfs->blocks[ch.data].file_ch_data.data[k];
				printf("%c ",isprint(v)?v:'.');
			}
				printf("  ");
			printf("\n");
			// if ((j+1)%16==0)
		}

		chunk_ptr = ch.next;
	}
	printf("\n");
}

void cfsWriteFile(CFS* cfs, fsptr file, char* text, int* err) {
	//TODO: errors
	FileHeader& f = PTR2FILE(file);
	fsptr fhptr = f.start;

	// Go to last chunk
	while (PTR2FHEAD(fhptr).next != 0)
		fhptr = PTR2FHEAD(fhptr).next;

	// Seek to end of chunk
	// FileChunkHeader fh = cfs->blocks[f.start].file_ch_head;
	int seek = PTR2FHEAD(fhptr).size;

	// LOGd(strlen(text));
	// LOGd(seek);

	for (int i=0;i<strlen(text);i++) {
		if (seek > CHUNK_SIZE) {
			printf("OH NO, THIS IS VERY BAD!\n");
			LOGd(seek);
			LOGd(CHUNK_SIZE);
			return;
		}
		if (seek == CHUNK_SIZE) {
			seek = 0;
			// printf("\n  Got to next chunk, creating a new one\n");
			f.chunks++;
			
			// Allocate next chunk
			fsptr next = cfsAlloc(cfs,err);
			fsptr next_data = cfsAlloc(cfs,err);

			// Zero-out the new blocks
			cfsZeroBlock(cfs,next);
			cfsZeroBlock(cfs,next_data);

			// Set ptrs for next chunk
			PTR2FHEAD(fhptr).next = next;
			PTR2FHEAD(next).data = next_data;

			
			// Move on to writing to the next chunk
			fhptr = next;
		}
		PTR2FDATA(PTR2FHEAD(fhptr).data).data[seek] = text[i];
		seek++;
		PTR2FHEAD(fhptr).size++;
	}
}

int cfsReadFile(CFS* cfs, fsptr file, char** text) {
	int size = 0;
	FileHeader fh = PTR2FILE(file);
	fsptr chunk_ptr = fh.start;

	// Count the sizes of all chunks
	for (int i=0;i<fh.chunks;i++) {
		FileChunkHeader ch = cfs->blocks[chunk_ptr].file_ch_head;
		
		size += ch.size;

		chunk_ptr = ch.next;
	}

	// Reset chunk_ptr after counting
	chunk_ptr = fh.start;

	*text = (char*)malloc(size);
	int seek=0;
	for (int i=0;i<fh.chunks;i++) {
		FileChunkHeader ch = cfs->blocks[chunk_ptr].file_ch_head;
		for (int j=0;j<ch.size;j++) {
			char v = PTR2FDATA(ch.data).data[j];
			(*text)[seek] = v;
			seek++;
		}

		chunk_ptr = ch.next;
	}

	return size;
}

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

fsptr cfsPath2Ptr(CFS* cfs, char* path, fsptr base, int* err) {
	// int part_count=0;
	// char **parts;
	char *token, *str, *tofree;
	tofree = str = strdup(path);  // We own str's memory now.
	// printf("im goin'\n");
	
	int i=-1;
	while ((token = strsep(&str,"/"))) {
		i++;
		// printf("tkn='%s' base=%d\n",token,base);
		if (strlen(token)==0)
			continue;
		if (strcmp(token,"..")==0) {
			// This *should* be a directory
			base = PTR2DIR(base).parent;
			// printf("going back\n");
			continue;
		}
		ListDirResults listdir = cfsListDir(cfs,PTR2DIR(base));
		// printf("	dirs: ");
		bool found=false;
		for (int j=0;j<listdir.dir_count;j++) {
			// printf("%s, ",listdir.dirs[j].name);
			if (strcmp(listdir.dirs[j].name,token)==0) {
				base = listdir.dir_ptrs[j];
				// printf("found it\n");
				found=true;
				break;
			}
		}
		if (found)
			continue;
		// printf("\n");
		// printf("	files: ");
		for (int j=0;j<listdir.file_count;j++) {
			// printf("%s, ",listdir.files[j].name);
			if (strcmp(listdir.files[j].name,token)==0) {
				base = listdir.file_ptrs[j];
				// printf("found it (file)\n");
				found=true;
				break;
			}
		}
		if (found)
			break;
		// printf("\n");
		// printf("oh no!\n");
	}
	free(tofree);
	return base;
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
	CFS cfs = cfsInit();
	// CFS cfs = cfsLoad("fs",&err);
	// cfsLogBAT(cfs);
	
	// fsptr cool_dir   = cfsCreateDir (&cfs, cfs.root, "F1", &err);
	// fsptr cool_file4 = cfsCreateFile(&cfs, cool_dir, "X", &err);
	// fsptr cool_file5 = cfsCreateFile(&cfs, cool_dir, "Y", &err);
	// fsptr cool_dir2  = cfsCreateDir (&cfs, cool_dir, "F2", &err);
	// fsptr cool_file6 = cfsCreateFile(&cfs, cool_dir2, "Z", &err);
	fsptr cool_file  = cfsCreateFile(&cfs, cfs.root, "A", &err);
	// fsptr cool_file2 = cfsCreateFile(&cfs, cfs.root, "B", &err);
	// fsptr cool_file3 = cfsCreateFile(&cfs, cfs.root, "C", &err);

	// if (err) {
	// 	printf("error %d :(\n",err);
	// }
	
	cfsLogBAT(cfs);

	printf("Tree:\n");
	cfsTree(&cfs,cfs.root,0);

	fsptr ptr=cfsPath2Ptr(&cfs,"A",cfs.root,&err);
	// LOGd(ptr);
	if (ptr == 0) {
		printf("I couldn't find the file :(\n");
		return 1;
	}
	// cfsPrintFile(&cfs,ptr);


	char text[] = R"(
hello worl!
i am 15 minutes old!
i am humgry

And now for lots of text (with newlines for fun) to show
that files are split into multiple chunks of size 128 (or 256 if
I'm feeling spicy) without compromising the integrity of the
underlying information. Wow, very cool.
)";

	printf("\n----------------------\nWriting!\n----------------------\n\n");
	cfsWriteFile(&cfs,ptr,text,&err);
	// cfsWriteFile(&cfs,ptr,"hello world!",&err);

	char* text2;
	printf("\n----------------------\nReading!\n----------------------\n\n");
	int n = cfsReadFile(&cfs,ptr,&text2);
	
	printf("Read (%d): %s\n",n,text2);
	
	cfsLogBAT(cfs);

	if (strcmp(text,text2)) {
		printf("They aren't the same :(\n");
	} else {
		printf("They are the same :) yipe!\n");
	}
	// cfsPrintFile(&cfs,ptr);

	// cfsSave(&cfs,"fs",&err);

	return 0;
}
