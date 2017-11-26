#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include <string.h>
#include <time.h>
#define EEXIST 17 /* File exists */
#define ENOENT 2  /* No such file or directory */
#define EISDIR 21

unsigned char *disk;
struct ext2_super_block *super_block;
struct ext2_group_desc *group_desc;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;
struct ext2_inode *inodes;
unsigned int inode_index_given_name_parent(struct ext2_inode *parent_inode, char* name, unsigned char type);
struct ext2_inode* inode_given_path(char *input);
void block_bitmap_alter(unsigned int ind, int result);
void inode_bitmap_alter(unsigned int ind, int result);

struct ext2_inode* inode_given_path(char *input){
	char real_path[1024];
	int path_len = strlen(input);
	strcpy(real_path, input);
	real_path[path_len + 1] = '\0';
	
	unsigned int num;
	
	// make the parent inode point to the root
	struct ext2_inode *parent_inode = &inodes[EXT2_ROOT_INO - 1];
	if (path_len != 1){
		char* temp = strtok(real_path, "/");
		while (temp != NULL){
			num = inode_index_given_name_parent(parent_inode, temp, EXT2_FT_DIR);
			printf("inode num : %d\n", num);
			if (num == 0){
				return NULL;
			}
			parent_inode = &inodes[num-1];
			temp = strtok(NULL, "/");
		}
	}
	else if (path_len == 1){
		if (real_path[0] == '/'){
			return parent_inode;		
		}	
	}
	return parent_inode;
}

int get_entry_size(char* name){
	int result = 8;
	if((strlen(name)%4) != 0){
		result += (strlen(name)/4)*4 + 4;	
	}else{
		result += strlen(name);
	}
	
	return result;
}

unsigned int inode_index_given_name_parent(struct ext2_inode *parent_inode, char* name, unsigned char type){
	
	struct ext2_dir_entry *entry;
	int count = 0;
   int memory_count;
   int flag1 = 0;
   int flag2 = 0; 
   int flag3 = 0;
   
   // loop through the inode's i_block
   while (count < 15){
   	if (parent_inode->i_block[count]!=0){
   		memory_count = 0;
   		entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[count]);
   		while(memory_count < EXT2_BLOCK_SIZE){
   			if (strlen(name) == entry->name_len){
   				flag1 = 1;
   			}
   			if (strncmp(name, entry->name, entry->name_len) == 0){
   				flag2 = 1;
   			}
   			if (type & entry->file_type){
   				flag3 = 1;
   			}
   			if (flag1 == 1 & flag2 == 1 & flag3 == 1){
   				printf("entry->inode %d \n", entry->inode);
   				return entry->inode;
   			}
				flag1 = 0;flag2 = 0;flag3 = 0;
				memory_count += entry->rec_len;
				entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[count] + memory_count);
  		 	}
   	}
   	count ++;
   }
   return 0;
}

int restore_dir_entry(struct ext2_inode *parent_inode, char* self_name){
	int memory_record;
	struct ext2_dir_entry *entry, *possible_entry;
	int restored_inode;
	int actual_size;
	int flag1 = 0, flag2 = 0, flag3 = 0;

	int block_count = (parent_inode->i_blocks)/2;
	
	printf("check parent : %d \n", parent_inode->i_links_count);
	printf("check self_name : %s \n", self_name);
	printf("check parent block: %d \n", block_count);

	for (int i = 0; i < block_count; i++){
		memory_record = 0;
		while (memory_record < EXT2_BLOCK_SIZE){
			entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE*parent_inode->i_block[i] + memory_record);
			actual_size = get_entry_size(entry->name);
			
			if(actual_size != entry->rec_len){
				
				 int start = memory_record + actual_size;
			    int end = memory_record + entry->rec_len;
			    while(start <= end){
			    		possible_entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE*parent_inode->i_block[i] + start);
			    		if (possible_entry->inode != 0){
			    			printf("possible name: %s \n", possible_entry -> name);
			    			printf("now position : %d \n", start);
			    			if (possible_entry->file_type == (unsigned char)EXT2_FT_REG_FILE) {
								flag1 = 1;
				 			}
				 			
				 			if(strncmp(self_name, possible_entry -> name, possible_entry->name_len) == 0){
				 				
				 				flag2 = 1;
				 			}
				 			
				 			if (possible_entry->inode != 0){
								flag3 = 1;
				 			}
				 			if (flag1 & flag2 & flag3){
				 				printf("this: %s \n", possible_entry -> name);
								entry -> rec_len = start - memory_record;
								restored_inode = possible_entry->inode;
								return restored_inode;
							}
			    		}
			    		flag1 = 0; flag2 = 0; flag3 = 0;
			    		start += 4;
			    }
				
			}
			memory_record += entry->rec_len;
		}
	}	
	return 0;
}

void block_bitmap_alter(unsigned int ind, int result){
	int off = ind / (sizeof(char*));
	int rem = ind % (sizeof(char*));
	
	unsigned char* position = block_bitmap + off;
	*position = ((*position & ~(1 << rem)) | (result << rem));
}

void inode_bitmap_alter(unsigned int ind, int result){
	int off = ind / (sizeof(char*));
	int rem = ind % (sizeof(char*));
	
	unsigned char* position = inode_bitmap + off;
	*position = ((*position & ~(1 << rem)) | (result << rem));
} 

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: <disk name> <absolute_path>\n");
        exit(1);
    }

    char* chosen_disk = argv[1];
    char* chosen_path = argv[2];
    
    int fd = open(chosen_disk, O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    // find useful component
    super_block = (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
    group_desc = (struct ext2_group_desc*)(disk + EXT2_BLOCK_SIZE*2);
    block_bitmap = (unsigned char*)(disk + EXT2_BLOCK_SIZE * group_desc->bg_block_bitmap);
    inode_bitmap = (unsigned char*)(disk + EXT2_BLOCK_SIZE * group_desc->bg_inode_bitmap);
    inodes = (struct ext2_inode*)(disk + EXT2_BLOCK_SIZE * group_desc->bg_inode_table);
    
    char* real_path = malloc(256); 
    int path_len = strlen(chosen_path);
	 if (chosen_path[path_len - 1] == '/'){
    	strcpy(real_path, chosen_path);
    	real_path[path_len + 1] = '\0';
    	real_path[path_len + 2] = '\0';
    } else if (chosen_path[path_len - 1] != '/'){
    	strcpy(real_path, chosen_path);
    	strcat(real_path, "/");
    	real_path[path_len + 1] = '\0';
    }

	 printf("real_path : %s \n", real_path);
    int k = strlen(real_path) - 2;
    while (k > -1 & real_path[k]!='/'){
    		k --;
    }
    //get parent directory
    char* parent_path;
    if (k < 0){
    	parent_path = malloc(2);
    	strcpy(parent_path, "/");
    	parent_path[1] = '\0';
    }else{
    	parent_path = malloc(k+1);
    	strncpy(parent_path, real_path, k+1);
    	parent_path[k+1] = '\0';
    }
	
	 printf("parent_path : %s \n", parent_path);
	 
    //get self name
    char copy_path[1024];
	 char cp[1024];
    strcpy(copy_path, real_path);
    char* temp = strtok(copy_path, "/");
	 while (temp != NULL){
		 strcpy(cp, temp);
		 temp = strtok(NULL, "/");
	 }
	 char* self_name = malloc(strlen(cp));
	 strcpy(self_name, cp);
	 self_name[strlen(cp)] = '\0';
	 
	 printf("self_name : %s \n", self_name);
	 
	 struct ext2_inode *parent_inode = inode_given_path(parent_path);
	 int restored_inode_index = restore_dir_entry(parent_inode, self_name);
	 
	 /*
	 struct ext2_dir_entry *entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE*parent_inode->i_block[0] + 24);
	 entry->rec_len = 40;*/
	 inode_bitmap_alter(restored_inode_index - 1, 1); 
	 


	 return 0;

}
