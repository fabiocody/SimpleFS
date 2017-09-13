//
//  simplefs.c
//  SimpleFS
//
//  Created by Fabio Codiglioni on 07/06/17.
//  Copyright © 2017 Fabio Codiglioni. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MARK: - Defines
// MARK: Constants
#define MAX_CHILDREN 1024
#define MAX_NAME_LEN 256
#define MAX_TREE_DEPTH 255
#define HASH_SIZE 1031
#define DIR_T 0
#define FILE_T 1
#define INVALID_KEY -1
// MARK: Macro
#define PARENT(i) (i - 1) / 2
#define LEFT(i) 2 * i + 1
#define RIGHT(i) 2 * i + 2
#define KEY_IS_INVALID(key) key < 0
#define KEY_IS_VALID(key) key >= 0


// MARK: Behavior defines
//#define AVALANCHE
//#define TEST


// MARK: - Data Structures

struct node {
	// Node of the main tree structure
	unsigned char type;					// Type of the node (file or directory)
	char *name;							// Filename
	unsigned short n_children;			// Number of children
	unsigned short level;				// Level of the node in the tree
	char *content;						// Content of the file (used only if the node is a file)
	struct node *parent;				// Pointer to the parent node
	struct node **children_hash;		// Array of pointers to struct node used as hash table for children (used only if the node is a directory)
};


// MARK: - Global Variables

struct node *root = NULL;					// Root of the tree
char *buffer = NULL;						// Buffer used to read the input
size_t buffer_size = 512;					// Size of the buffer above
char *path_buffer = NULL;					// Buffer used to reconstruct paths (used in FSfind)
size_t path_buffer_size = 512;				// Size of the buffer above
unsigned int max_level = 0;					// Maximum level of the tree
struct node *tombstone;						// Global tombstone used in hash tables
unsigned long long total_resources = 0;		// Total resources counter (used in FSfind, root is excluded from count)


// MARK: - Hash functions

unsigned int hash_function(char *string) {		// O(k) since names have finite length
	// Actual hash function
	unsigned int key = 0x1713;
	while (*string)	key = (key << 3) ^ (key * *string++);
	return key;
}


unsigned int double_hash(char *string, unsigned int step) {		// O(k)
	// Handle probing for closed hashing
	unsigned int key = hash_function(string);		// O(k)
	if (step == 0) return key % HASH_SIZE;
	else return (key + step * (1 + (key % (HASH_SIZE - 1)))) % HASH_SIZE;
}


int hash_insert(struct node **hash_table, struct node *node) {     // O(HASH_SIZE) -> O(k)
	// Insert a node into a hash table
	int key;
	for (unsigned int i = 0; i < HASH_SIZE; i++) {
		key = double_hash(node->name, i);		// O(k)
		if (hash_table[key] == NULL || hash_table[key] == tombstone) {
			hash_table[key] = node;
			return key;
		}
	}
	return INVALID_KEY;
}


int hash_lookup(struct node **hash_table, char *string) {      // O(HASH_SIZE) -> O(k)
	// Perform a lookup within a hash table
	// Return INVALID_KEY if no element with name equal to string is found
	int key;
	for (unsigned int i = 0; i < HASH_SIZE; i++) {
		key = double_hash(string, i);
		if (hash_table[key] == NULL)
			break;
		else if (hash_table[key] != tombstone && strcmp(hash_table[key]->name, string) == 0)
			return key;
	}
	return INVALID_KEY;
}


int hash_delete(struct node **hash_table, char *string, unsigned char freeup_element) {      // O(HASH_SIZE) -> O(k)
	// Delete a node from a hash table
	// The freeup_element argument is used to free the node besides removing it from the hash table. This comes handy in FSdelete_r, since I can delete a node from the parent's hash table and then free up the subtree with no worries
	int key = hash_lookup(hash_table, string);		// O(k)
	if (KEY_IS_VALID(key)) {
		if (freeup_element) {
			free(hash_table[key]->content);
			free (hash_table[key]->children_hash);
			free(hash_table[key]->name);
			free(hash_table[key]);
		}
		hash_table[key] = tombstone;
		return key;
	}
	return INVALID_KEY;
}


// MARK: - Other functions

char *get_next_token(char *tokenized_path) {        // O(1)
	// Get next token from a tokenized string (i.e. with all spaces replaced by 0s)
	unsigned long len;
	if (tokenized_path != NULL && (len = strlen(tokenized_path) + 1) && tokenized_path[len] != '\0')
		return &tokenized_path[len];
	else
		return NULL;
}


char *get_filename(char *tokenized_path) {      // O(path_depth)
	// Get the resource's name from a tokenized path (i.e. the last resource's name)
	char *token = get_next_token(tokenized_path);
	char *prev_token = NULL;
	while (token != NULL) {		// O(path_depth)
		prev_token = token;
		token = get_next_token(token);
	}
	return prev_token;
}


char *get_parent_name(char *tokenized_path) {       // O(path_depth)
	// Get the parent's name from a tokenized path (i.e. the penultimate resource's name)
	char *token = get_next_token(tokenized_path);
	char *prev_token = NULL;
	char *filename = get_filename(tokenized_path);		// O(path_depth)
	if (token == filename)
		return "";
	while (token != filename) {		// O(path_depth)
		prev_token = token;
		token = get_next_token(token);
	}
	return prev_token;
}


struct node *file_init(char *tokenized_path, struct node *parent) {       // O(path_depth)
	// Initialize a file node
	struct node *new_file = (struct node *)calloc(1, sizeof(struct node));
	if (new_file == NULL) return NULL;
	new_file->type = FILE_T;
	char *filename;
	if (strcmp(tokenized_path, "tombstone") == 0) {
		new_file->name = (char *)malloc((strlen("tombstone") + 1) * sizeof(char));
		if (new_file->name == NULL) exit(EXIT_FAILURE);
		strcpy(new_file->name, "tombstone");
	}
	else {
		filename = get_filename(tokenized_path);	// O(path_depth)
		new_file->name = (char *)malloc((strlen(filename) + 1) * sizeof(char));
		if (new_file->name == NULL) exit(EXIT_FAILURE);
		strcpy(new_file->name, filename);
	}
	new_file->n_children = 0;
	if (parent) new_file->level = parent->level + 1;
	else new_file->level = 0;
	new_file->parent = parent;
	return new_file;
}


struct node *dir_init(char *tokenized_path, struct node *parent) {        // O(path_depth)
	// Initialize a directory node
	struct node *new_dir = (struct node *)calloc(1, sizeof(struct node));
	if (new_dir == NULL) return NULL;
	new_dir->type = DIR_T;
	char *filename = get_filename(tokenized_path);		// O(path_depth)
	if (filename == NULL) {
		new_dir->name = (char *)malloc(2 * sizeof(char));
		if (new_dir->name == NULL) exit(EXIT_FAILURE);
		strcpy(new_dir->name, "");
	} else {
		new_dir->name = (char *)malloc((strlen(filename) + 1) * sizeof(char));
		if (new_dir->name == NULL) exit(EXIT_FAILURE);
		strcpy(new_dir->name, filename);
	}
	new_dir->n_children = 0;
	new_dir->parent = parent;
	if (parent) new_dir->level = parent->level + 1;
	else new_dir->level = 0;
	new_dir->children_hash = (struct node **)calloc(HASH_SIZE, sizeof(struct node *));
	if (new_dir->children_hash == NULL) exit(EXIT_FAILURE);
	return new_dir;
}


char *read_from_stdin(void) {       // O(strlen(input))
	// This function reads from stdin.
	unsigned int i = 0;
	memset(buffer, 0, buffer_size);
	while (1) {
		for (; i < buffer_size - 4; i++) {
			buffer[i] = getc(stdin);
			if (buffer[i] == '\n') {
				buffer[i] = '\0';
				return buffer;
			}
		}
		if (buffer[i - 1] != '\n') {
			// If the line is not over but you've nearly reached a buffer overflow, expand the buffer
			buffer_size <<= 1;
			buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
			if (buffer == NULL) exit(EXIT_FAILURE);
			memset(&buffer[i], 0, buffer_size-i);
		}
	}
}


struct node *get_child(struct node* node, char *filename) {     // O(k)
	// Get child node from a parent node
	if (node == NULL || filename == NULL || node->type == FILE_T)
		return NULL;
	int key = hash_lookup(node->children_hash, filename);		// O(k)
	if (KEY_IS_VALID(key))
		return node->children_hash[key];
	else
		return NULL;
}


unsigned short path_level(char *tokenized_path) {		// O(path_depth)
	// Get the level of the node specified by the path provided
	char *token = get_next_token(tokenized_path);
	unsigned short level = 0;
	while (token != NULL) {		// O(path_depth)
		token = get_next_token(token);
		level += 1;
	}
	return level;
}


struct node *walk(char *tokenized_path) {       // O(path_depth)
	// Walk through the tree until the last valid position (i.e. actual node or parent)
	// Return NULL if no valid position is found. This could be because a midway node is absent.
	struct node *curr_node = root, *prev_node = NULL;
	char *token = get_next_token(tokenized_path);
	while (curr_node != NULL) {		// O(path_depth)
		prev_node = curr_node;
		curr_node = get_child(curr_node, token);
		token = get_next_token(token);
	}
	// If I walked throughout the path, the node is valid, otherwise return NULL
	unsigned int counter = 0;
	while (token != NULL) {		// O(path_depth)
		token = get_next_token(token);
		counter += 1;
	}
	if (counter > 0)
		return NULL;
	return prev_node;
}


void delete_recursive(struct node *node) {      // O(HASH_SIZE) -> O(k)			recursive call = O(#children)
	// Recursively delete a subtree
	if (node->type == DIR_T) {
		for (unsigned short i = 0; i < HASH_SIZE; i++)
			if (node->children_hash[i] != NULL && node->children_hash[i] != tombstone)
				delete_recursive(node->children_hash[i]);
	}
	free(node->content);
	free(node->children_hash);
	free(node->name);
	free(node);
	total_resources--;
}


char *reconstruct_path(struct node *node) {     // O(path_depth)
	// Recursively reconstruct the path of a node (used in FSfind)
	char new_name[MAX_NAME_LEN] = {0};
	if (path_buffer_size < buffer_size || path_buffer == NULL) {
		path_buffer_size = buffer_size;
		path_buffer = (char *)realloc(path_buffer, path_buffer_size);
		if (path_buffer == NULL) exit(EXIT_FAILURE);
	}
	strcat(new_name, "/");
	strcat(new_name, node->name);
	if (node != root) strcat(reconstruct_path(node->parent), new_name);
	return path_buffer;
}


void swap(char *array[], long long i, long long j) {		// O(1)
	char *temp = array[i];
	array[i] = array[j];
	array[j] = temp;
}


long long partition(char *array[], long long lo, long long hi) {		// Theta(hi)
	char *pivot = array[hi];
	long long i = lo - 1;
	for (long long j = lo; j < hi; j++) {
		if (strcmp(array[j], pivot) <= 0) {
			i += 1;
			if (i != j)
				swap(array, i, j);
		}
	}
	swap(array, i+1, hi);
	return i+1;
}


void quicksort(char *array[], long long lo, long long hi) {		// O(hi log(hi))
	if (lo < hi) {
		long long p = partition(array, lo, hi);
		quicksort(array, lo, p-1);
		quicksort(array, p+1, hi);
	}
}


void find_recursive(struct node *node, char *name, char **results, size_t *results_size) {      // O(tree_depth)		recursive call = O(results_size * tree_depth)
	// Recursively find node with name equal to the name provided (used in FSfind)
	if (node != NULL) {
		if (strcmp(name, node->name) == 0) {
			if (path_buffer != NULL) memset(path_buffer, 0, path_buffer_size);
			char *temp_path = reconstruct_path(node);	// O(path_depth)
			char *path = (char *)malloc((strlen(temp_path) + 1) * sizeof(char));
			if (path == NULL) exit(EXIT_FAILURE);
			strcpy(path, temp_path);
			results[*results_size] = path;
			*results_size += 1;
		}
		if (node->type == DIR_T) {
			for (unsigned int i = 0; i < HASH_SIZE; i++)
				if (node->children_hash[i] != NULL)
					find_recursive(node->children_hash[i], name, results, results_size);
		}
	}
}


// MARK: - Testing functions


#ifdef TEST
void walk_recursive(struct node *node) {
	// JUST FOR TESTING
	// Walk throughtout the tree and print whatever found
	if (path_buffer != NULL)
		memset(path_buffer, 0, path_buffer_size);
	if (node == root)
		printf("%p - /\n", node);
	else
		printf("%p - %s\n", node, reconstruct_path(node));
	if (node->type == DIR_T) {
		for (unsigned int i = 0; i < HASH_SIZE; i++)
			if (node->children_hash[i] != NULL && node->children_hash[i] != tombstone)
				walk_recursive(node->children_hash[i]);
	}
}
#else
void walk_recursive(struct node *node) {
	puts("no");
}
#endif // TEST


#ifdef TEST
void ls(char *tokenized_path) {
	// JUST FOR TESTING
	// If the resource specified by the path is a directory, print all its children
	// If no path is provided, then print all root's content
	struct node *node = walk(tokenized_path);
	if (node->type == FILE_T)
		return;
	for (unsigned short i = 0; i < HASH_SIZE; i++) {
		if (node->children_hash[i] != NULL)
			printf("%p - %s\n", node->children_hash[i], node->children_hash[i]->name);
	}
	return;
}
#else
void ls(char *tokenized_path) {
	puts("no");
}
#endif // TEST


#ifdef TEST
void node_level(char *tokenized_path) {
	// JUST FOR TESTING
	// Print level of the node specified by the path provided
	struct node *node = walk(tokenized_path);
	if (node != NULL && path_level(tokenized_path) == node->level)
		printf("ok level=%u\n", node->level);
	else
		puts("no");
}
#else
void node_level(char *tokenized_path) {
	puts("no");
}
#endif // TEST


// MARK: - Functions (FS commands)

void FScreate(char *tokenized_path) {       // O(path)
	// File system command: create
	struct node *parent = walk(tokenized_path);		// O(path)
	if (parent == NULL) {
		puts("no");
		return;
	}
	struct node *new_file = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	int key;
	if (strcmp(parent_name, parent->name) == 0 && parent->type == DIR_T && parent->level < MAX_TREE_DEPTH - 1 && parent->n_children < MAX_CHILDREN) {
		new_file = file_init(tokenized_path, parent);
		if (new_file != NULL) {
			key = hash_insert(parent->children_hash, new_file);
			if (KEY_IS_VALID(key)) {
				parent->n_children += 1;
				if (max_level < new_file->level) max_level = new_file->level;
				total_resources += 1;
				puts("ok");
				return;
			}
		}
		else exit(EXIT_FAILURE);
	}
	puts("no");
}


void FScreate_dir(char *tokenized_path) {       // O(path)
	// File system command: create_dir
	struct node *parent = walk(tokenized_path);		// O(path)
	struct node *new_dir = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	int key;
	if (parent != NULL && strcmp(parent_name, parent->name) == 0 && parent->type == DIR_T && parent->level < MAX_TREE_DEPTH - 1 && parent->n_children < MAX_CHILDREN) {
		new_dir = dir_init(tokenized_path, parent);
		if (new_dir != NULL) {
			key = hash_insert(parent->children_hash, new_dir);
			if (KEY_IS_VALID(key)) {
				parent->n_children += 1;
				if (max_level < new_dir->level) max_level = new_dir->level;
				total_resources += 1;
				puts("ok");
				return;
			}
		}
		else exit(EXIT_FAILURE);
	}
	puts("no");
}


void FSread(char *tokenized_path) {         // O(path + file_content)
	// File system command: read
	struct node *file = walk(tokenized_path);	// O(path)
	char *filename = get_filename(tokenized_path);	// O(path)
	if (file != NULL && file->type == FILE_T && strcmp(filename, file->name) == 0) {
		printf("contenuto %s\n", file->content != NULL ? file->content : "");
		return;
	}
	puts("no");
}


void FSwrite(char *tokenized_path, char *content) {     // O(path + file_content)
	// File system command: write
	struct node *file = walk(tokenized_path);	// O(path)
	char *filename = get_filename(tokenized_path);		// O(path)
	unsigned long long content_len = strlen(content);
	if (file != NULL && file->type == FILE_T && strcmp(filename, file->name) == 0) {
		file->content = (char *)realloc(file->content, (content_len + 1) * sizeof(char));
		if (file->content == NULL) exit(EXIT_FAILURE);
		//memset(file->content, 0, content_len + 1);
		strcpy(file->content, content);
		printf("ok %llu\n", content_len);
		return;
	}
	puts("no");
}


void FSdelete(char *tokenized_path) {     // O(path_depth)
	// File system command: delete
	struct node *node = walk(tokenized_path);	// O(path_depth)
	struct node *parent = NULL;
	char *filename = get_filename(tokenized_path);		// O(path_depth)
	int key;
	if (node != NULL && strcmp(filename, node->name) == 0 && node->n_children == 0 && path_level(tokenized_path) == node->level) {		// O(path_depth)
		parent = node->parent;
		key = hash_delete(parent->children_hash, filename, 1);		// O(k)
		if (KEY_IS_VALID(key)) {
			parent->n_children--;
			total_resources--;
			puts("ok");
			return;
		}
	}
	puts("no");
}


void FSdelete_r(char *tokenized_path) {       // O(#children + path_depth)
	// File system command: delete_r
	struct node *node = walk(tokenized_path);		// O(path_depth)
	struct node *parent = NULL;
	char *filename = get_filename(tokenized_path);		// O(path_depth)
	int key;
	if (node != NULL && strcmp(filename, node->name) == 0) {
		parent = node->parent;
		key = hash_delete(parent->children_hash, filename, 0);		// O(k)
		if (KEY_IS_VALID(key))
			parent->n_children--;
		delete_recursive(node);		// O(#children)
		puts("ok");
		return;
	}
	puts("no");
}


void FSfind(char *name) {       // O(results_size * tree_depth + results_size * log(results_size))
	// File system command: find
	char **results = NULL;
	size_t results_size = 0;
	results = (char **)malloc(total_resources * sizeof(char **));
	if (results == NULL) exit(EXIT_FAILURE);
	results[0] = NULL;
	find_recursive(root, name, results, &results_size);		// O(results_size * tree_depth)
	if (results[0] == NULL) puts("no");
	quicksort(results, 0, results_size-1);		// O(results_size * log(results_size))
	for (size_t i = 0; i < results_size; i++) {
		printf("ok %s\n", results[i]);
		free(results[i]);
	}
	free(results);
}


// MARK: - Main

int main(int argc, char *argv[]) {
	
	// MARK: Initializations
	char ROOT_NAME[3] = "/";
	char *command = NULL, *path = NULL, *content = NULL;
	
	buffer = (char *)malloc(buffer_size * sizeof(char));
	if (buffer == NULL) exit(EXIT_FAILURE);
	root = dir_init(ROOT_NAME, NULL);
	tombstone = file_init("tombstone", NULL);
	
	// MARK: Main Loop
	while (1) {
		
		// Get input
		path = content = NULL;
		command = buffer = read_from_stdin();
		
		// Prepare input for handling and set path pointer
		size_t i = 0;
		for (; i < buffer_size && buffer[i] != '"' && buffer[i] != 0; i++) {
			if (buffer[i] == ' ' || buffer[i] == '/') {
				buffer[i] = 0;
				if (path == NULL) {
					do {
						buffer[i] = 0;
						i++;
					} while (i < buffer_size-1 && (buffer[i+1] == ' ' || buffer[i+1] == '/'));
					path = &buffer[i];
					if (buffer[i] == '/') buffer[i] = 0;
				}
			}
		}
		
		// If a content part is present, remove quotes and set content pointer
		if (buffer[i] == '"') {
			buffer[i] = 0;
			content = &buffer[i + 1];
			for (; i < buffer_size && buffer[i] != '"'; i++);
			buffer[i] = 0;
		}
		
		// MARK: Simulated switch
		if (strcmp(command, "create") == 0) {
			FScreate(path);
		} else if (strcmp(command, "create_dir") == 0) {
			FScreate_dir(path);
		} else if (strcmp(command, "read") == 0) {
			FSread(path);
		} else if (strcmp(command, "write") == 0) {
			FSwrite(path, content);
		} else if (strcmp(command, "delete") == 0) {
			FSdelete(path);
		} else if (strcmp(command, "delete_r") == 0) {
			FSdelete_r(path);
		} else if (strcmp(command, "find") == 0) {
			FSfind(path);
		} else if (strcmp(command, "exit") == 0) {
			break;
		} else if (strcmp(command, "ls") == 0) {
			ls(path);
		} else if (strcmp(command, "du") == 0) {
			walk_recursive(root);
		} else if (strcmp(command, "level") == 0) {
			node_level(path);
		} else {
			puts("no");
		}
		
	}
	
	return 0;
	
}
