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
#define MAX_TREE_DEPTH 256
#define HASH_DIMENSION 1031
#define DIR_T 0
#define FILE_T 1
#define INVALID_KEY -1
#define TOMBSTONE -2
// MARK: Macro
#define KEY_IS_INVALID(key) key < 0
#define KEY_IS_VALID(key) key >= 0
#define NODE_ALLOC (struct node *)calloc(1, sizeof(struct node))
//#define CHECK_MALLOC(ptr) if (ptr == NULL) exit(EXIT_FAILURE);
#define BUFFER_ZERO(buffer, size) for (unsigned long long i = 0; i < size; i++) {buffer[i] = 0;}



// MARK: Behavior defines
#define AVALANCHE
//#define TEST


// MARK: - Data Structures

struct index_node {		// Index used in FSfind
	int key;
	struct index_node *left;
	struct index_node *right;
	struct index_node *parent;
};

struct node {
	unsigned char type;
	char name[MAX_NAME_LEN];
	unsigned short children_no;
	unsigned short level;
	char *content;
	struct node *parent;
	struct bucket *children_hash;
	struct index_node *key_index;
};

struct bucket {
	int key;
	struct node *child;
};




// MARK: - Global Variables

struct node *root = NULL;
unsigned char STOP_READING = 0;
char *buffer = NULL;
size_t buffer_size = 1024;
char *path_buffer = NULL;
size_t path_buffer_size = 1024;
unsigned long long total_resources = 1;
unsigned int max_level = 1;
size_t pathstrlen;




// MARK: - BST functions

void KIinorder_walk(struct index_node *node) {
	if (node != NULL) {
		KIinorder_walk(node->left);
		printf("%d\n", node->key);
		KIinorder_walk(node->right);
	}
}


struct index_node *KImin(struct index_node *node) {
	while (node != NULL && node->left != NULL)
		node = node->left;
	return node;
}


struct index_node *KImax(struct index_node *node) {
	while (node->right != NULL)
		node = node->right;
	return node;
}


struct index_node *KIsearch(struct index_node *node, int key) {
	if (node == NULL || node->key == key) return node;
	if (key < node->key) return KIsearch(node->left, key);
	return KIsearch(node->right, key);
}


struct index_node *KInext(struct index_node *x) {
	if (x->right != NULL)
		return KImin(x->right);
	struct index_node *y = x->parent;
	while (y != NULL && y->right == x) {
		x = y;
		y = y->parent;
	}
	return y;
}


struct index_node *KIinsert(struct index_node *root, int key) {
	struct index_node *prev = NULL;
	struct index_node *curr = root;
	while (curr != NULL) {
		prev = curr;
		if (key < curr->key)
			curr = curr->left;
		else curr = curr->right;
	}
	struct index_node *new = (struct index_node *)calloc(1, sizeof(struct index_node));
	if (new == NULL) {
		perror("malloc failed");
		exit(EXIT_FAILURE);
	}
	new->parent = prev;
	new->key = key;
	if (prev == NULL)
		return new;
	if (key < prev->key)
		prev->left = new;
	else prev->right = new;
	return root;
}


struct index_node *KIdelete(struct index_node *root, int key) {
	struct index_node *x = KIsearch(root, key);
	struct index_node *tobedeleted = NULL;
	struct index_node *subtree = NULL;
	if (x->left == NULL || x->right == NULL)
		tobedeleted = x;
	else tobedeleted = KInext(x);
	if (tobedeleted->left != NULL)
		subtree = tobedeleted->left;
	else subtree = tobedeleted->right;
	if (subtree != NULL)
		subtree->parent = tobedeleted->parent;
	if (tobedeleted->parent == NULL) {
		free(tobedeleted);
		return subtree;
	}
	if (tobedeleted == tobedeleted->parent->left)
		tobedeleted->parent->left = subtree;
	else tobedeleted->parent->right = subtree;
	if (tobedeleted != x)
		x->key = tobedeleted->key;
	free(tobedeleted);
	return root;
}


struct index_node *KIdestroy(struct index_node *node) {
	if (node != NULL) {
		KIdestroy(node->left);
		KIdestroy(node->right);
		free(node);
	}
	return NULL;
}


// MARK: - Hash functions

unsigned int djb2(char *string) {       // O(1), because strings have finite length (255)
	unsigned int key = 5381;
	int c;
	while ((c = *string++))     // exit when *str == '\0'
		key = ((key << 5) + key) + c;
	#ifdef AVALANCHE
	key = (key+0x479ab41d) + (key<<8);
	key = (key^0xe4aa10ce) ^ (key>>5);
	key = (key+0x9942f0a6) - (key<<14);
	key = (key^0x5aedd67d) ^ (key>>3);
	key = (key+0x17bea992) + (key<<7);
	#endif // AVALANCHE
	return key;
}


unsigned int djb2_opt(char *string) {
	unsigned int key = 5381;
	unsigned long len = strlen(string);
	unsigned int *string_x4 = (unsigned int *)string;
	size_t i = len >> 2;
	if (len > 3) {
		do {
			key = ((key << 5) + key) + *string_x4++;
		} while (--i);
	}
	if (len & 3) {
		i = len & 3;
		string = (char *)&string[i - 1];
		do {
			key = ((key << 5) + key) + *string--;
		} while (--i);
	}
	#ifdef AVALANCHE
	key = (key+0x479ab41d) + (key<<8);
	key = (key^0xe4aa10ce) ^ (key>>5);
	key = (key+0x9942f0a6) - (key<<14);
	key = (key^0x5aedd67d) ^ (key>>3);
	key = (key+0x17bea992) + (key<<7);
	#endif // AVALANCHE
	return ((key & 0xff000000) >> 24) + ((key & 0xff0000) >> 16) + ((key & 0xff00) >> 8) + (key & 0xff);
}


unsigned int double_hash(char *string, unsigned int step) {		// O(1)
	unsigned int key = djb2(string);
	if (step == 0)
		return key & 0x3ff;		// key % 1024
	else
		return (key + step * (1 + ((key & 0x3ff) - 1))) & 0x3ff;
}


int hash_insert(struct bucket *hash_table, struct node *node) {     // O(HASH_DIMENSION) ≈ O(k)
	unsigned int orig_key = double_hash(node->name, 0);
	int key = orig_key;
	for (unsigned int i = 0; i < HASH_DIMENSION; i++) {
		if (i > 0)
			key = double_hash(node->name, i);
		if (KEY_IS_INVALID(hash_table[key].key)) {
			hash_table[key].child = node;
			hash_table[key].key = orig_key;
			return key;     // SUCCESS
		}
	}
	return INVALID_KEY;		// FAILURE
}


int hash_lookup(struct bucket *hash_table, char *string) {      // O(HASH_DIMENSION) ≈ O(k)
	int key;
	for (unsigned int i = 0; i < HASH_DIMENSION; i++) {
		key = double_hash(string, i);
		if (hash_table[key].key == INVALID_KEY)
			break;
		if (hash_table[key].key != TOMBSTONE)
			if (strcmp(hash_table[key].child->name, string) == 0)
				return key;
	}
	return INVALID_KEY;
}


int hash_delete(struct bucket *hash_table, char *string, unsigned char freeup_element) {      // O(HASH_DIMENSION) ≈ O(k)
	int key = hash_lookup(hash_table, string);
	if (KEY_IS_VALID(key)) {
		hash_table[key].key = TOMBSTONE;
		if (freeup_element) {
			struct node *node = hash_table[key].child;
			free(node->content);
			free (node->children_hash);
			KIdestroy(node->key_index);
			free(node);
			total_resources--;
		}
		hash_table[key].child = NULL;
		return key;
	}
	return INVALID_KEY;
}


struct bucket *build_hash_table() {     // O(1)
	struct bucket *hash_table = (struct bucket *)calloc(HASH_DIMENSION, sizeof(struct bucket));
	if (hash_table == NULL)
		return NULL;
	for (unsigned int i = 0; i < HASH_DIMENSION; i++)
		hash_table[i].key = INVALID_KEY;
	return hash_table;
}


// MARK: - Quicksort

void swap(char *array[], long long i, long long j) {
	char *temp = array[i];
	array[i] = array[j];
	array[j] = temp;
}


long long partition(char *array[], long long lo, long long hi) {
	char *pivot = array[hi];
	long long i = lo - 1;
	for (long long j = lo; j < hi; j++) {
		if (strcmp(array[j], pivot) <= 0) {
			i++;
			if (i != j)
				swap(array, i, j);
		}
	}
	swap(array, i+1, hi);
	return i+1;
}


void quicksort(char *array[], long long lo, long long hi) {
	if (lo < hi) {
		long long p = partition(array, lo, hi);
		quicksort(array, lo, p-1);
		quicksort(array, p+1, hi);
	}
}


// MARK: - Other functions

void prepare_path_tokens(char *path) {      // O(strlen(path)) ≈ O(k)
	// Replace all the slashes with nulls
	for (unsigned int i = 0; i < pathstrlen; i++) {
		if (path[i] == '/')
			path[i] = '\0';
	}
}


char *get_next_token(char *tokenized_path) {        // O(1)
	if (tokenized_path == NULL)
		return NULL;
	else if (tokenized_path[strlen(tokenized_path) + 1] != '\0')
		return &tokenized_path[strlen(tokenized_path) + 1];
	else
		return NULL;
}


char *get_filename(char *tokenized_path) {      // O(pathlen)
	char *token;
	if (pathstrlen > 64) {
		token = &tokenized_path[pathstrlen - 1];
		while (*(--token));
		token++;
		return token;
	} else if (pathstrlen > 0) {
		token = get_next_token(tokenized_path);
		char *prev_token = NULL;
		while (token != NULL) {
			prev_token = token;
			token = get_next_token(token);
		}
		return prev_token;
	} else {
		return NULL;
	}
}


char *get_parent_name(char *tokenized_path) {       // O(pathlen)
	char *token = get_next_token(tokenized_path)/*, *prev_token = NULL*/;
	char *filename = get_filename(tokenized_path);
	if (token == filename)
		return "";
	if (pathstrlen > 64) {
		token = &tokenized_path[pathstrlen - strlen(filename) - 1];
		while (*(--token));
		token++;
		return token;
	} else {
		char *prev_token = NULL;
		while (token != filename) {
			prev_token = token;
			token = get_next_token(token);
		}
		return prev_token;
	}
}


struct node *file_init(char *tokenized_path, struct node *parent, unsigned short level) {       // O(1)
	struct node *new_file = NODE_ALLOC;
	if (new_file == NULL)
		return NULL;
	new_file->type = FILE_T;
	strcpy(new_file->name, get_filename(tokenized_path));
	new_file->children_no = 0;
	new_file->level = level;
	new_file->parent = parent;
	return new_file;
}


struct node *dir_init(char *tokenized_path, struct node *parent, unsigned short level) {        // O(1)
	struct node *new_dir = NODE_ALLOC;
	if (new_dir == NULL)
		return NULL;
	new_dir->type = DIR_T;
	char *filename = get_filename(tokenized_path);
	if (filename == NULL) {
		strcpy(new_dir->name, "");
	} else {
		strcpy(new_dir->name, filename);
	}
	new_dir->children_no = 0;
	new_dir->level = level;
	new_dir->parent = parent;
	new_dir->children_hash = build_hash_table();
	if (new_dir->children_hash == NULL) {
		free(new_dir);
		return NULL;
	}
	return new_dir;
}


char *read_from_stdin(void) {       // O(strlen(input))
	unsigned int i;
	BUFFER_ZERO(buffer, buffer_size);
	i = 0;
	while (1) {
		for (; i < buffer_size - 4; i++) {
			buffer[i] = getc(stdin);
			if (buffer[i] == '\n') {
				buffer[i] = buffer[i + 1] = '\0';
				return buffer;
			} else if (buffer[i] == EOF) {
				STOP_READING = 1;
				return buffer;
			}
		}
		if (buffer[i - 1] != '\n') {
			buffer_size *= 2;
			buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
			if (buffer == NULL)
				return NULL;
		}
	}
}


struct node *get_child(struct node* node, char *filename) {     // O(k)
	if (node == NULL || filename == NULL || node->type == FILE_T)
		return NULL;
	int key = hash_lookup(node->children_hash, filename);
	if (KEY_IS_VALID(key))
		return node->children_hash[key].child;
	else
		return NULL;
}


unsigned short path_level(char *tokenized_path) {		// O(pathlen)
	char *token = get_next_token(tokenized_path);
	unsigned short counter = 1;
	while (token != NULL) {
		token = get_next_token(token);
		counter++;
	}
	return counter;
}


struct node *walk(char *tokenized_path) {       // O(pathlen)
	// Walk through the tree until the last valid position
	struct node *curr_node = root, *prev_node = NULL;
	char *token = get_next_token(tokenized_path);
	while (curr_node != NULL) {
		prev_node = curr_node;
		curr_node = get_child(curr_node, token);
		token = get_next_token(token);
	}
	unsigned int counter = 0;
	while (token != NULL) {
		token = get_next_token(token);
		counter++;
	}
	if (counter > 0)
		return NULL;
	return prev_node;
}


unsigned short level(struct node *node) {   // O(pathlen)
	unsigned short counter = 1;
	if (node == NULL)
		return 0;
	while (node->parent != NULL) {
		node = node->parent;
		counter++;
	}
	return counter;
}


void delete_recursive(struct node *node) {      // O(children number)
	if (node->type == DIR_T) {
		for (unsigned short i = 0; i < HASH_DIMENSION; i++)
			if (node->children_hash[i].child != NULL)
				delete_recursive(node->children_hash[i].child);
	}
	free(node->content);
	free (node->children_hash);
	KIdestroy(node->key_index);
	free(node);
	total_resources--;
}


char *reconstruct_path(struct node *node) {     // O(pathlen)
	char new_name[MAX_NAME_LEN] = {0};
	if (path_buffer_size < buffer_size || path_buffer == NULL) {
		path_buffer_size = buffer_size;
		path_buffer = (char *)realloc(path_buffer, path_buffer_size);
		/* $$$ if (path_buffer == NULL)
			return NULL;*/
	}
	strcat(new_name, "/");
	strcat(new_name, node->name);
	if (node != root)
		strcat(reconstruct_path(node->parent), new_name);
	return path_buffer;
}


void find_recursive(struct node *node, char *name, unsigned long long *index, char **results) {      // O(found * pathlen + total resources)
	if (node != NULL) {
		if (strcmp(name, node->name) == 0) {
			if (path_buffer != NULL)
				BUFFER_ZERO(path_buffer, path_buffer_size);
			results[*index] = (char *)calloc(max_level * MAX_NAME_LEN, sizeof(char));
			if (results[*index] == NULL) {
				puts("no");
				return;
			}
			strcpy(results[*index], reconstruct_path(node));
			(*index)++;
		}
		if (node->type == DIR_T) {
			struct index_node *curr = KImin(node->key_index);
			while (curr != NULL) {
				find_recursive(node->children_hash[curr->key].child, name, index, results);
				curr = KInext(curr);
			}
			/*for (unsigned int i = 0; i < HASH_DIMENSION; i++)
				if (node->children_hash[i].child != NULL)
					find_recursive(node->children_hash[i].child, name, index, results);*/
		}
	}
}


#ifdef TEST
void walk_recursive(struct node *node) {        // O(children number)
	if (path_buffer != NULL)
		BUFFER_ZERO(path_buffer, path_buffer_size);
	if (node == root)
		printf("%p - /\n", node);
	else
		printf("%p - %s\n", node, reconstruct_path(node));
	if (node->type == DIR_T) {
		for (unsigned int i = 0; i < HASH_DIMENSION; i++)
			if (node->children_hash[i].child != NULL)
				walk_recursive(node->children_hash[i].child);
	}
}
#else
void walk_recursive(struct node *node) {
	puts("no");
}
#endif // TEST




// MARK: - Functions (FS commands)

#ifdef TEST
void ls(char *tokenized_path) {
	struct node *node = walk(tokenized_path);
	if (node->type == FILE_T)
		return;
	for (unsigned short i = 0; i < HASH_DIMENSION; i++) {
		if (node->children_hash[i].child != NULL)
			printf("%u - %p - %s\n", node->children_hash[i].key, node->children_hash[i].child, node->children_hash[i].child->name);
	}
	return;
}
#else
void ls(char *tokenized_path) {
	puts("no");
}
#endif // TEST


void FScreate(char *tokenized_path) {       // O(path)
	struct node *parent = walk(tokenized_path);		// O(path)
	if (parent == NULL) {
		puts("no");
		return;
	}
	struct node *new_file = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	unsigned short parent_level = level(parent);		// O(path)
	int key;
	if (strcmp(parent_name, parent->name) == 0 &&
		parent->type == DIR_T &&
		parent_level < MAX_TREE_DEPTH &&
		parent->children_no < MAX_CHILDREN) {
		new_file = file_init(tokenized_path, parent, parent_level + 1);
		if (new_file != NULL) {
			key = hash_insert(parent->children_hash, new_file);
			if (KEY_IS_VALID(key)) {
				parent->key_index = KIinsert(parent->key_index, key);
				parent->children_no++;
				total_resources++;
				if (max_level < parent_level + 1)
					max_level = parent_level + 1;
				puts("ok");
				return;
			}
		}
	}
	puts("no");
}


void FScreate_dir(char *tokenized_path) {       // O(path)
	struct node *parent = walk(tokenized_path);		// O(path)
	struct node *new_dir = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	unsigned short parent_level = level(parent);		// O(path)
	int key;
	if (parent != NULL && strcmp(parent_name, parent->name) == 0 &&
		parent->type == DIR_T &&
		parent_level < MAX_TREE_DEPTH &&
		parent->children_no < MAX_CHILDREN) {
		new_dir = dir_init(tokenized_path, parent, parent_level + 1);
		if (new_dir != NULL) {
			key = hash_insert(parent->children_hash, new_dir);
			if (KEY_IS_VALID(key)) {
				parent->key_index = KIinsert(parent->key_index, key);
				parent->children_no++;
				total_resources++;
				if (max_level < parent_level + 1)
					max_level = parent_level + 1;
				puts("ok");
				return;
			}
		}
	}
	puts("no");
}


void FSread(char *tokenized_path) {         // O(path + file_content)
	struct node *file = walk(tokenized_path);	// O(path)
	char *filename = get_filename(tokenized_path);	// O(path)
	if (file != NULL && file->type == FILE_T) {
		if (strcmp(filename, file->name) == 0) {
			printf("contenuto %s\n", file->content != NULL ? file->content : "");
			return;
		}
	}
	puts("no");
}


void FSwrite(char *tokenized_path, char *content) {     // O(path + file_content)
	struct node *file = walk(tokenized_path);	// O(path)
	char *filename = get_filename(tokenized_path);		// O(path)
	unsigned long long content_len = strlen(content);
	if (file != NULL && file->type == FILE_T) {
		if (strcmp(filename, file->name) == 0) {
			/*if (file->content != NULL) {
				free(file->content);
				file->content = NULL;
			}*/
			file->content = (char *)realloc(file->content, (content_len + 1) * sizeof(char));
			if (file->content != NULL) {
				BUFFER_ZERO(file->content, content_len + 1);
				strcpy(file->content, content);
				printf("ok %llu\n", content_len);
				return;
			}
		}
	}
	puts("no");
}


void FSdelete(char *tokenized_path) {     // O(path)
	struct node *node = walk(tokenized_path);	// O(path)
	struct node *parent = NULL;
	char *filename = get_filename(tokenized_path);		// O(path)
	int key;
	if (node != NULL) {
		if (strcmp(filename, node->name) == 0) {
			parent = node->parent;
			if (node->children_no == 0 && path_level(tokenized_path) == node->level) {
				key = hash_delete(parent->children_hash, filename, 1);
				if (KEY_IS_VALID(key)) {
					parent->key_index = KIdelete(parent->key_index, key);
					parent->children_no--;
					puts("ok");
					return;
				}
			}
		}
	}
	puts("no");
}


void FSdelete_r(char *tokenized_path) {       // O(# children)
	struct node *node = walk(tokenized_path);
	struct node *parent = NULL;
	char *filename = get_filename(tokenized_path);
	int key;
	if (node != NULL) {
		if (filename == NULL) {
			if (node == root) {
				delete_recursive(root);
				return;
			}
		} else if (strcmp(filename, node->name) == 0) {
			parent = node->parent;
			key = hash_delete(parent->children_hash, filename, 0);
			if (KEY_IS_VALID(key)) {
				parent->key_index = KIdelete(parent->key_index, key);
				parent->children_no--;
				//total_resources--;
			}
			delete_recursive(node);
			puts("ok");
			return;
		}
	}
	puts("no");
}


void FSfind(char *name) {       // O(# total resources + (found resources)^2)
	unsigned long long index = 0;
	char *results[total_resources];
	find_recursive(root, name, &index, results);
	if (index == 0)
		puts("no");
	else {
		quicksort(results, 0, index - 1);
		for (unsigned long long i = 0; i < index; i++)
			printf("ok %s\n", results[i]);
	}
	for (unsigned long long i = 0; i < index; i++)
		free(results[i]);
}




// MARK: - Main

int main(int argc, char *argv[]) {
	
	// MARK: Initializations
	char ROOT_NAME[3] = "/";
	char *command = NULL, *path = NULL, *content = NULL;
	unsigned char verbose = 0;
	buffer = (char *)calloc(buffer_size, sizeof(char));
	if (buffer == NULL)
		exit(EXIT_FAILURE);
	root = dir_init(ROOT_NAME, NULL, 1);
	
	if (argc >= 2) {
		if (strcmp(argv[1], "-v") == 0)
			verbose = 1;
	}
	
	if (verbose) printf("Root addr = %p\n\n", root);
	
	// MARK: Main Loop
	while (1) {
		
		// MARK: Read
		command = path = content = NULL;
		buffer = read_from_stdin();
		if (buffer == NULL || STOP_READING || buffer[0] == '\0')
			break;
		
		// MARK: Replace spaces with nulls throughout the array (except for content part)
		for (unsigned short i = 0; i < buffer_size && buffer[i] != '"'; i++) {      // walk through the whole array or just until the start of the <content> part (if present)
			if (buffer[i] == ' ')
				buffer[i] = '\0';
		}
		
		// MARK: Set command pointer
		command = buffer;
		
		// MARK: Set path pointer
		for (unsigned short i = 0; i < buffer_size; i++) {
			if (buffer[i] == '\0') {
				for (; buffer[i] == '\0'; i++);
				path = &buffer[i];
				break;
			}
		}
		
		pathstrlen = strlen(path);
		
		// MARK: Set content pointer and remove quotes
		for (unsigned int i = 0; i < buffer_size - strlen(command) - 1; i++) {
			if (path[i] == '\0' && path[i + 1] == '"') {
				path[i + 1] = '\0';
				content = &path[i + 2];
				for (unsigned int j = 0; j < buffer_size - strlen(command) - pathstrlen - 2; j++) {
					if (content[j] == '"')
						content[j] = '\0';
				}
				break;
			}
		}
		
		// MARK: Debug prints
		if (verbose) {
			printf("command = %s\n", command);
			printf("path = %s\n", path);
			printf("content = %s\n", content);
		}
		
		// MARK: Tokenize path
		prepare_path_tokens(path);
		
		// MARK: Switch
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
		} else if (strcmp(command, "ls") == 0) {
			ls(path);
		} else if (strcmp(command, "du") == 0) {
			walk_recursive(root);
		} else if (strcmp(command, "exit") == 0) {
			break;
		} else {
			puts("no");
		}
		
		if (verbose) puts("");
		
	}
	
	/*BUFFER_ZERO(buffer, buffer_size);
	strcpy(buffer, "/");
	prepare_path_tokens(buffer);
	FSdelete_r(buffer);
	free(buffer);
	free(path_buffer);*/
	
	//printf("buffer_size = %llu\n", buffer_size);
	
	return 0;
}
