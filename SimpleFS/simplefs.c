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
#define HASH_DIMENSION 1031
#define HASH_MAGIC_NUMBER 0x1505
#define DIR_T 0
#define FILE_T 1
#define INVALID_KEY -1
#define TOMBSTONE -2
// MARK: Macro
#define KEY_IS_INVALID(key) key < 0
#define KEY_IS_VALID(key) key >= 0


// MARK: Behavior defines
//#define AVALANCHE
//#define TEST
//#define CLEANUP


// MARK: - Data Structures

struct node {
	unsigned char type;
	char *name;
	unsigned short children_no;
	unsigned short level;
	char *content;
	struct node *parent;
	struct node **children_hash;
};


struct bst_node {
	char *path;
	struct bst_node *left;
	struct bst_node *right;
};


// MARK: - Global Variables

struct node *root = NULL;
char *buffer = NULL;
size_t buffer_size = 1024;
char *path_buffer = NULL;
size_t path_buffer_size = 1024;
unsigned int max_level = 0;
size_t pathstrlen;
struct node *tombstone;


// MARK: - Hash functions

unsigned int djb2(char *string) {       // O(1), because strings have finite length (255)
	unsigned int key = HASH_MAGIC_NUMBER;
	unsigned char byte;
	while ((byte = *string++))     // exit when *str == '\0'
		key = ((key << 5) + key) + byte;
	return key;
}


unsigned int double_hash(char *string, unsigned int step) {		// O(1)
	unsigned int key = djb2(string);
	if (step == 0)
		return key % HASH_DIMENSION;
	else
		return (key + step * (1 + (key % (HASH_DIMENSION - 1)))) % HASH_DIMENSION;
}


int hash_insert(struct node **hash_table, struct node *node) {     // O(HASH_DIMENSION) ≈ O(k)
	int key;
	for (unsigned int i = 0; i < HASH_DIMENSION; i++) {
		key = double_hash(node->name, i);
		if (hash_table[key] == NULL || hash_table[key] == tombstone) {
			hash_table[key] = node;
			return key;     // SUCCESS
		}
	}
	return INVALID_KEY;		// FAILURE
}


int hash_lookup(struct node **hash_table, char *string) {      // O(HASH_DIMENSION) ≈ O(k)
	int key;
	for (unsigned int i = 0; i < HASH_DIMENSION; i++) {
		key = double_hash(string, i);
		if (hash_table[key] == NULL)
			break;
		else if (hash_table[key] != tombstone)
			if (strcmp(hash_table[key]->name, string) == 0)
				return key;
	}
	return INVALID_KEY;
}


int hash_delete(struct node **hash_table, char *string, unsigned char freeup_element) {      // O(HASH_DIMENSION) ≈ O(k)
	int key = hash_lookup(hash_table, string);
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


struct node **build_hash_table() {     // O(1)
	struct node **hash_table = (struct node **)calloc(HASH_DIMENSION, sizeof(struct node *));
	return hash_table;
}


// MARK: - BST functions

struct bst_node *bst_insert(struct bst_node *root, char *path) {
	struct bst_node *new_node = NULL;
	struct bst_node *prev = NULL;
	struct bst_node *curr = root;
	new_node = (struct bst_node *)calloc(1, sizeof(struct bst_node));
	if (new_node == NULL)
		exit(EXIT_FAILURE);
	new_node->path = path;
	while (curr != NULL) {
		prev = curr;
		if (strcmp(path, curr->path) < 0) curr = curr->left;
		else curr = curr->right;
	}
	if (prev == NULL) return new_node;
	else if (strcmp(path, prev->path) < 0) prev->left = new_node;
	else prev->right = new_node;
	return root;
}


void bst_in_order_print(struct bst_node *node) {
	if (node != NULL) {
		bst_in_order_print(node->left);
		printf("ok %s\n", node->path);
		bst_in_order_print(node->right);
	}
}


void bst_destroy(struct bst_node *node) {
	if (node != NULL) {
		bst_destroy(node->left);
		bst_destroy(node->right);
		free(node->path);
		free(node);
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
	/*if (pathstrlen > 64) {
		token = &tokenized_path[pathstrlen - 1];
		while (*(--token));
		token++;
		return token;
	} else if (pathstrlen > 0) {*/
		token = get_next_token(tokenized_path);
		char *prev_token = NULL;
		while (token != NULL) {
			prev_token = token;
			token = get_next_token(token);
		}
		return prev_token;
	//}
}


char *get_parent_name(char *tokenized_path) {       // O(pathlen)
	char *token = get_next_token(tokenized_path)/*, *prev_token = NULL*/;
	char *filename = get_filename(tokenized_path);
	if (token == filename)
		return "";
	/*if (pathstrlen > 64) {
		token = &tokenized_path[pathstrlen - strlen(filename) - 1];
		while (*(--token));
		token++;
		return token;
	} else {*/
		char *prev_token = NULL;
		while (token != filename) {
			prev_token = token;
			token = get_next_token(token);
		}
		return prev_token;
	//}
}


struct node *file_init(char *tokenized_path, struct node *parent) {       // O(1)
	struct node *new_file = (struct node *)calloc(1, sizeof(struct node));
	if (new_file == NULL)
		return NULL;
	new_file->type = FILE_T;
	char *filename = get_filename(tokenized_path);
	if (filename == NULL) {
		new_file->name = (char *)calloc(strlen("tombstone") + 1, sizeof(char));
		if (new_file->name == NULL)
			exit(EXIT_FAILURE);
		strcpy(new_file->name, "tombstone");
	}
	else {
		new_file->name = (char *)calloc(strlen(filename) + 1, sizeof(char));
		if (new_file->name == NULL)
			exit(EXIT_FAILURE);
		strcpy(new_file->name, filename);
	}
	new_file->children_no = 0;
	if (parent)
		new_file->level = parent->level + 1;
	else
		new_file->level = 0;
	new_file->parent = parent;
	return new_file;
}


struct node *dir_init(char *tokenized_path, struct node *parent) {        // O(1)
	struct node *new_dir = (struct node *)calloc(1, sizeof(struct node));
	if (new_dir == NULL)
		return NULL;
	new_dir->type = DIR_T;
	char *filename = get_filename(tokenized_path);
	if (filename == NULL) {
		new_dir->name = (char *)calloc(2, sizeof(char));
		if (new_dir->name == NULL)
			exit(EXIT_FAILURE);
		strcpy(new_dir->name, "");
	} else {
		new_dir->name = (char *)calloc(strlen(filename) + 1, sizeof(char));
		if (new_dir->name == NULL)
			exit(EXIT_FAILURE);
		strcpy(new_dir->name, filename);
	}
	new_dir->children_no = 0;
	new_dir->parent = parent;
	if (parent)
		new_dir->level = parent->level + 1;
	else
		new_dir->level = 0;
	new_dir->children_hash = build_hash_table();
	if (new_dir->children_hash == NULL)
		exit(EXIT_FAILURE);
	return new_dir;
}


void buffer_zero(char *buffer, size_t size) {
	/*if (size % sizeof(unsigned long long) == 0) {
		unsigned long long *ptr = (unsigned long long *)buffer;
		size /= sizeof(unsigned long long);
		for (size_t i = 0; i < size; i++)
			ptr[i] = 0;
	} else {
		for (size_t i = 0; i < size; i++)
			buffer[i] = 0;
	}*/
	memset(buffer, 0, size);
}


char *read_from_stdin(void) {       // O(strlen(input))
	unsigned int i = 0;
	buffer_zero(buffer, buffer_size);
	while (1) {
		for (; i < buffer_size - 4; i++) {
			buffer[i] = getc(stdin);
			if (buffer[i] == '\n') {
				buffer[i] = buffer[i + 1] = '\0';
				return buffer;
			}
		}
		if (buffer[i - 1] != '\n') {
			buffer_size *= 2;
			buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
			if (buffer == NULL)
				exit(EXIT_FAILURE);
		}
	}
}


struct node *get_child(struct node* node, char *filename) {     // O(k)
	if (node == NULL || filename == NULL || node->type == FILE_T)
		return NULL;
	int key = hash_lookup(node->children_hash, filename);
	if (KEY_IS_VALID(key))
		return node->children_hash[key];
	else
		return NULL;
}


unsigned short path_level(char *tokenized_path) {		// O(pathlen)
	char *token = get_next_token(tokenized_path);
	unsigned short level = 0;
	while (token != NULL) {
		token = get_next_token(token);
		level++;
	}
	return level;
}


struct node *walk(char *tokenized_path) {       // O(pathlen)
	// Walk through the tree until the last valid position (i.e. actual node or parent)
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


void delete_recursive(struct node *node) {      // O(children number)
	if (node->type == DIR_T) {
		for (unsigned short i = 0; i < HASH_DIMENSION; i++)
			if (node->children_hash[i] != NULL)
				delete_recursive(node->children_hash[i]);
	}
	free(node->content);
	free(node->children_hash);
	free(node->name);
	free(node);
}


char *reconstruct_path(struct node *node) {     // O(pathlen)
	char new_name[MAX_NAME_LEN] = {0};
	if (path_buffer_size < buffer_size || path_buffer == NULL) {
		path_buffer_size = buffer_size;
		path_buffer = (char *)realloc(path_buffer, path_buffer_size);
		if (path_buffer == NULL)
			exit(EXIT_FAILURE);
	}
	strcat(new_name, "/");
	strcat(new_name, node->name);
	if (node != root)
		strcat(reconstruct_path(node->parent), new_name);
	return path_buffer;
}


void find_recursive(struct node *node, char *name, struct bst_node **bst_root) {      // O(found * pathlen + total resources)
	if (node != NULL) {
		if (strcmp(name, node->name) == 0) {
			if (path_buffer != NULL)
				buffer_zero(path_buffer, path_buffer_size);
			char *temp_path = reconstruct_path(node);
			char *path = (char *)calloc(strlen(temp_path) + 1, sizeof(char));
			if (path == NULL)
				exit(EXIT_FAILURE);
			strcpy(path, temp_path);
			*bst_root = bst_insert(*bst_root, path);
		}
		if (node->type == DIR_T) {
			for (unsigned int i = 0; i < HASH_DIMENSION; i++)
				if (node->children_hash[i] != NULL)
					find_recursive(node->children_hash[i], name, bst_root);
		}
	}
}


#ifdef TEST
void walk_recursive(struct node *node) {        // O(children number)
	if (path_buffer != NULL)
		buffer_zero(path_buffer, path_buffer_size);
	if (node == root)
		printf("%p - /\n", node);
	else
		printf("%p - %s\n", node, reconstruct_path(node));
	if (node->type == DIR_T) {
		for (unsigned int i = 0; i < HASH_DIMENSION; i++)
			if (node->children_hash[i] != NULL)
				walk_recursive(node->children_hash[i]);
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


void FScreate(char *tokenized_path) {       // O(path)
	struct node *parent = walk(tokenized_path);		// O(path)
	if (parent == NULL) {
		puts("no");
		return;
	}
	struct node *new_file = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	int key;
	if (strcmp(parent_name, parent->name) == 0 &&
		parent->type == DIR_T &&
		parent->level < MAX_TREE_DEPTH - 1 &&
		parent->children_no < MAX_CHILDREN) {
		new_file = file_init(tokenized_path, parent);
		if (new_file != NULL) {
			key = hash_insert(parent->children_hash, new_file);
			if (KEY_IS_VALID(key)) {
				parent->children_no++;
				if (max_level < new_file->level)
					max_level = new_file->level;
				puts("ok");
				return;
			}
		} else {
			exit(EXIT_FAILURE);
		}
	}
	puts("no");
}


void FScreate_dir(char *tokenized_path) {       // O(path)
	struct node *parent = walk(tokenized_path);		// O(path)
	struct node *new_dir = NULL;
	char *parent_name = get_parent_name(tokenized_path);		// O(path)
	int key;
	if (parent != NULL && strcmp(parent_name, parent->name) == 0 &&
		parent->type == DIR_T &&
		parent->level < MAX_TREE_DEPTH - 1 &&
		parent->children_no < MAX_CHILDREN) {
		new_dir = dir_init(tokenized_path, parent);
		if (new_dir != NULL) {
			key = hash_insert(parent->children_hash, new_dir);
			if (KEY_IS_VALID(key)) {
				parent->children_no++;
				if (max_level < new_dir->level)
					max_level = new_dir->level;
				puts("ok");
				return;
			}
		} else {
			exit(EXIT_FAILURE);
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
			file->content = (char *)realloc(file->content, (content_len + 1) * sizeof(char));
			if (file->content != NULL) {
				buffer_zero(file->content, content_len + 1);
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
				parent->children_no--;
			}
			delete_recursive(node);
			puts("ok");
			return;
		}
	}
	puts("no");
}


void FSfind(char *name) {       // O(# total resources + (found resources)^2)
	struct bst_node *bst_root = NULL;
	find_recursive(root, name, &bst_root);
	if (bst_root == NULL)
		puts("no");
	else
		bst_in_order_print(bst_root);
	bst_destroy(bst_root);
}


#ifdef TEST
void node_level(char *tokenized_path) {
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


// MARK: - Main

int main(int argc, char *argv[]) {
	
	// MARK: Initializations
	char ROOT_NAME[3] = "/";
	char *command = NULL, *path = NULL, *content = NULL;
	
	buffer = (char *)calloc(buffer_size, sizeof(char));
	if (buffer == NULL)
		exit(EXIT_FAILURE);
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
			if (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == '/') {
				buffer[i] = 0;
				if (path == NULL) {
					do {
						buffer[i] = 0;
						i++;
					} while (i < buffer_size-1 && (buffer[i+1] == ' ' || buffer[i+1] == '\t' || buffer[i+1] == '/'));
					path = &buffer[i];
					if (buffer[i] == '/')
						buffer[i] = 0;
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
		} else if (strcmp(command, "level") == 0) {
			node_level(path);
		} else {
			puts("no");
		}
		
	}
	
	#ifdef CLEANUP
	buffer_zero(buffer, buffer_size);
	strcpy(buffer, "/");
	prepare_path_tokens(buffer);
	FSdelete_r(buffer);
	free(buffer);
	free(path_buffer);
	#endif // CLEANUP
	
	return 0;
	
}
