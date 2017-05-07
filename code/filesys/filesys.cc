// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"
#include <stdio.h>
#include <string.h>
// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
//#define NumDirEntries 		10
#define NumDirEntries 		64
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);    
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

//lihsin
bool
FileSystem::Create(char *name, int initialSize)
{
	Directory *directory;
	PersistentBitmap *freeMap;
    FileHeader *hdr;
	OpenFile *openFileDir;
    int sector;
    bool success;
    char *pch;
	int cntNull = 0,count =0;

    pch = strtok(name,"/");
	directory = new Directory(NumDirEntries);
	directory->FetchFrom(directoryFile);
	
    while(cntNull == 0){
        //printf("pch = %s/n",pch);
		if (directory->Find(pch) != -1){
			//printf("indirect\n");
            openFileDir = new OpenFile(directory->Find(pch));
			directory->FetchFrom(openFileDir);
            success = FALSE;          // file is already in directory
        }
        else{ 
			//printf("last pch\n");
            freeMap = new PersistentBitmap(freeMapFile,NumSectors);
            sector = freeMap->FindAndSet(); // find a sector to hold the file header
            if (sector == -1)       
                success = FALSE;        // no free block for file header 
            else if (!directory->Add(pch, sector))
                success = FALSE;    // no space in directory
            else {
                hdr = new FileHeader;
				if (!hdr->Allocate(freeMap, initialSize))
						success = FALSE;    // no space on disk for data
				else 
				{  
					//printf("success\n");
					success = TRUE;
					//printf("sector = %d\n",sector);
					hdr->WriteBack(sector); 
					if (count==0){
						//printf("count = %d\n",count);
						directory->WriteBack(directoryFile);
					}
					else directory->WriteBack(openFileDir);
					freeMap->WriteBack(freeMapFile);
				}
				//printf("delete hdr\n");
				delete hdr;
			}
            
        }
        pch = strtok(NULL,"/");   
		//printf("pch = %s/n",pch);
		if (pch==NULL) cntNull++;
		count++;
    }
	//printf("delete directory\n");
	delete directory;
	delete freeMap;
	//printf("delete directory\n");
	
    //delete openFileDir;
    return success;
}
bool
FileSystem::CreateDir(char *name)
{
	Directory *directory;
	Directory *newDir;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
	OpenFile *openFileDir;
    int sector;
    bool success;
    char *pch;
    int count = 0;

    pch = strtok(name,"/");

    while(pch != NULL){
        //printf("%s\n", pch);
        directory = new Directory(NumDirEntries);
		
		
        if(count==0){
            directory->FetchFrom(directoryFile);
        }
        else{
            directory->FetchFrom(openFileDir);
        }

        if (directory->Find(pch) != -1){
            openFileDir = new OpenFile(directory->Find(pch));
            success = FALSE;          // file is already in directory
        }
        else{ 
            freeMap = new PersistentBitmap(freeMapFile,NumSectors);
            sector = freeMap->FindAndSet(); // find a sector to hold the file header
            if (sector == -1)       
                success = FALSE;        // no free block for file header 
            else if (!directory->AddDir(pch, sector))
                success = FALSE;    // no space in directory
            else {
                hdr = new FileHeader;
				if (!hdr->Allocate(freeMap, DirectoryFileSize))
						success = FALSE;    // no space on disk for data
				else {  
					success = TRUE;
					hdr->WriteBack(sector); 
					if (count==0){
						directory->WriteBack(directoryFile);
					}
					else directory->WriteBack(openFileDir);
					freeMap->WriteBack(freeMapFile);

					newDir = new Directory(NumDirEntries);
					// everthing worked, flush all changes back to disk
					openFileDir = new OpenFile(sector);
					newDir->WriteBack(openFileDir);
				}
					delete hdr;
					delete newDir;
				}
            delete freeMap;
        }
        
        delete directory;
        pch = strtok(NULL,"/");   
        count ++;   
    }
    delete openFileDir;
    return success;
}
//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{ /*
	Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;

    DEBUG(dbgFile, "Opening file" << name);
    directory->FetchFrom(directoryFile);
    sector = directory->Find(name); 
    if (sector >= 0) 		
	openFile = new OpenFile(sector);	// name was found in directory 
    delete directory;
    return openFile;				// return NULL if not found

    */
	Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;
	OpenFile *openFileDir;
    char *pch;
	
    pch = strtok(name,"/");
    DEBUG(dbgFile, "Opening file" << name);
    directory->FetchFrom(directoryFile);
	//printf("name = %d \n",name);
	while(pch != NULL){
		//printf("pch = %d \n",pch);
		//sector = directory->Find(name);
		sector = directory->Find(pch);
		if (sector >= 0){
            openFileDir = new OpenFile(sector);
			directory->FetchFrom(openFileDir);
        }
        pch = strtok(NULL,"/"); 
		//
    }
	
    delete directory;
    return openFileDir;
    				// return NULL if not found

}

OpenFileId 
FileSystem::OpenId(char *name)
{
    int fd = 0;
    OpenFile* file = this->Open(name);
    
    while(fd <= 20) {
        fd ++;
        if(!OpenTable[fd]) {
            OpenTable[fd] = file;
            break;
        }
    }
    ASSERT(fd <= 20);

    return fd;
}   
int 
FileSystem::kwrite(char *buffer, int size, OpenFileId id)
{
    OpenFile* file = OpenTable[id];
    return file->Write(buffer, size);				// return NULL if not found	
}    
int 
FileSystem::kread (char *buffer, int size, OpenFileId id)
{
	OpenFile* file = OpenTable[id];
    return file->Read(buffer, size);

}     
int 
FileSystem::kclose(OpenFileId id)
{
	OpenFile* file = OpenTable[id];
    
    if(file == NULL)
        return 0;
    delete file;

    OpenTable[id] = NULL;
    
    return 1;
} 

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name)
{ 
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    int sector;
    
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    sector = directory->Find(name);
    if (sector == -1) {
       delete directory;
       return FALSE;			 // file not found 
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new PersistentBitmap(freeMapFile,NumSectors);

	
    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    directory->Remove(name);
	printf("%s \n",name);
    freeMap->WriteBack(freeMapFile);		// flush to disk
    directory->WriteBack(directoryFile);        // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
} 
bool
FileSystem::RRemove(char *name, int place, int Nfirst)
{
    Directory *directory;
    OpenFile *openFileDir;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    char *pch,*pch2;
    char name2[100];//=malloc(strlen(name)+1);
    strcpy(name2,name);
    int fileNum = 0;
    bool ButtomIsFile = false;
    directory = new Directory(NumDirEntries);
    if(!Nfirst){
		pch = strtok(name,"/");
		directory->FetchFrom(directoryFile);
		while(pch != NULL){       
			place = directory->Find(pch);
			if (place == -1) {
			   delete directory;
			   return FALSE;			 // file not found 
			}
			for (int i = 0; i < 64; i++){
				if (directory->table[i].inUse && strcmp(directory->table[i].name,pch) == 0){
					if(directory->table[i].isDir){
						openFileDir = new OpenFile(place);
						directory->FetchFrom(openFileDir);
					}
					else{
						ButtomIsFile = true;	
					}
				}
				
			 }	
			pch = strtok(NULL,"/"); 
			if(pch != NULL){
				fileNum++;
			}
		}
    }
    else{
		if (place > 0){
			openFileDir = new OpenFile(place);
			directory->FetchFrom(openFileDir);
		}
    }
	
	int total = 0;
	int sector;
	int j = 0;
	for (int i = 0; i < 64; i++){
		if (directory->table[i].inUse){			
			sector = directory->Find(directory->table[i].name);
			if(directory->table[i].isDir && ButtomIsFile == false){
				printf("--In %s--\n",directory->table[i].name);
				RRemove(directory->table[i].name,sector,1);
				
				printf("Remove[%d] %s D\n", j,directory->table[i].name);
				fileHdr = new FileHeader;
				fileHdr->FetchFrom(sector);

				freeMap = new PersistentBitmap(freeMapFile,NumSectors);

				fileHdr->Deallocate(freeMap);  		// remove data blocks
				freeMap->Clear(sector);	
				directory->Remove(directory->table[i].name);
				freeMap->WriteBack(freeMapFile);		// flush to disk
				directory->WriteBack(openFileDir);        // flush to disk
			}
			else if(!directory->table[i].isDir){
				printf("Remove[%d] %s F\n", j,directory->table[i].name);
				fileHdr = new FileHeader;
				fileHdr->FetchFrom(sector);

				freeMap = new PersistentBitmap(freeMapFile,NumSectors);

				fileHdr->Deallocate(freeMap);  		// remove data blocks
				freeMap->Clear(sector);	
				directory->Remove(directory->table[i].name);
				freeMap->WriteBack(freeMapFile);		// flush to disk
				directory->WriteBack(openFileDir);        // flush to disk
			}
		j++;
		}
	}
	int count = 0;
	if(!Nfirst && ButtomIsFile == false){
		pch2 = strtok(name2,"/");
		directory->FetchFrom(directoryFile);
		while(pch2 != NULL && count < fileNum){       
			place = directory->Find(pch2);
			if (place == -1) {
			   delete directory;
			   return FALSE;			 // file not found 
			}
			if (place > 0){
				openFileDir = new OpenFile(place);
				directory->FetchFrom(openFileDir);
			}
			pch2 = strtok(NULL,"/"); 
			count++;
		}

		sector = directory->Find(pch2);
			
		printf("Remove[%d] %s D\n", j,pch2);
					
		fileHdr = new FileHeader;
		fileHdr->FetchFrom(sector);

		freeMap = new PersistentBitmap(freeMapFile,NumSectors);

		fileHdr->Deallocate(freeMap);  		// remove data blocks
		freeMap->Clear(sector);	
		directory->Remove(pch2);
		freeMap->WriteBack(freeMapFile);		// flush to disk
		if(fileNum == 0)
			directory->WriteBack(directoryFile);        // flush to disk
		else
			directory->WriteBack(openFileDir);        // flush to disk

		delete fileHdr;
		delete directory;
		delete freeMap;		
	}	
}
//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List(char *name)
{
    Directory *directory;
    OpenFile *openFileDir;
    char *pch;
    
    pch = strtok(name,"/");
	directory = new Directory(NumDirEntries);
	directory->FetchFrom(directoryFile);
	
    while(pch != NULL){       
		if (directory->Find(pch) > 0){
            openFileDir = new OpenFile(directory->Find(pch));
			directory->FetchFrom(openFileDir);
        }
		//printf("list : %s",pch);
        pch = strtok(NULL,"/"); 
    }

    directory->List();
    delete directory;
}

void
FileSystem::RList(char *name, int place, int Nfirst)
{
	Directory *directory;
    OpenFile *openFileDir;
    Directory *Next;
	directory = new Directory(NumDirEntries);
	Next = new Directory(NumDirEntries);
	
	
    if(!Nfirst){
		char *pch;
		pch = strtok(name,"/");
		directory->FetchFrom(directoryFile);
		while(pch != NULL){       
			place = directory->Find(pch);
			if (place > 0){
				openFileDir = new OpenFile(place);
				directory->FetchFrom(openFileDir);
			}
			pch = strtok(NULL,"/"); 
		}
		directory->List();
	}
    else{
		if (place > 0){
			openFileDir = new OpenFile(place);
			directory->FetchFrom(openFileDir);
			directory->List();
		}
	}
	
	int count = 0;
	int fileNum=0;
	int total = 0;
	int sector;
	
	for (int i = 0; i < 64; i++){
			if (directory->table[i].inUse){
				if(directory->table[i].isDir){
					count++;
					total++;
				}
				else{
					total++;
					fileNum++;
				}
			}
	}
	if(fileNum!=total){
		printf("in file\n");
		count=0;
		fileNum=0;
		total = 0;
		int j =0;
		for (int i = 0; i < 64; i++){
			if (directory->table[i].inUse){
				if(directory->table[i].isDir){
					count++;
					total++;
					printf("--In %s--\n",directory->table[i].name);
					sector = directory->Find(directory->table[i].name);
					RList(directory->table[i].name,sector,1);
				}
				else{
					fileNum++;
					total++;
				}
			
			}
			j++;
		}
	}
}
//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 

#endif // FILESYS_STUB
