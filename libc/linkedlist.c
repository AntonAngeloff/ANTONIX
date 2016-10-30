/*
 * linkedlist.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <types.h>
#include <stddef.h>
#include <mm.h>
#include <linkedlist.h>

DOUBLE_LIST *dlist_create()
{
	DOUBLE_LIST *list;

	if(!(list = kcalloc(sizeof(DOUBLE_LIST)))) {
		return NULL;
	}

	/* The list is zeroed by kcalloc() so no need to
	 * populate anything.
	 *
	 * Node count and root node pointer are NULL.
	 */
	return list;
}

uint32_t dlist_add(DOUBLE_LIST *list, VOID *payload)
{
	DOUBLE_LIST_ENTRY* new_node;

	if(!(new_node = dlist_entry_create(payload))) {
		/* Out of memory perhaps */
		return 0;
	}

	if(!list->root_node) {
		/* List empty */
		list->root_node = new_node;
	} else {
		/* Append new node at the end */
		DOUBLE_LIST_ENTRY *current_node = list->root_node;

		while(current_node->next) {
			current_node = current_node->next;
		}

		/* Append */
		current_node->next = new_node;
		new_node->prev = current_node;
	}

	/* Update count */
	list->node_count++;

	return list->node_count;
}

VOID *dlist_get_at(DOUBLE_LIST* list, uint32_t index)
{
    if(index >= list->node_count || list->root_node == NULL) {
    	/* Index out of bounds */
    	return NULL;
    }

    /* Iterate through the items */
    DOUBLE_LIST_ENTRY* current_node = list->root_node;

    //Iteration, making sure we don't hang on malformed lists
    for(uint32_t i = 0; i < index; i++) {
        current_node = current_node->next;

        if (current_node == NULL) {
        	/* List is corrupted */
        	return NULL;
        }
    }

    /* Return payload */
    return current_node ? current_node->payload : NULL;
}

VOID *dlist_remove_at(DOUBLE_LIST* list, uint32_t index)
{
	VOID *payload;

	/* Validate index */
	if(index >= list->node_count || list->root_node == NULL) {
		return NULL;
	}

	/* Iterate items */
	DOUBLE_LIST_ENTRY* current_node = list->root_node;

	for(uint32_t i = 0; (i < index); i++) {
		current_node = current_node->next;

		if (current_node == NULL) {
			return NULL;
		}
	}
	/* Reorganize node neighbour pointer */
	if(current_node->prev) {
		current_node->prev->next = current_node->next;
	}

	if(current_node->next) {
		current_node->next->prev = current_node->prev;
	}

	if(index == 0) {
		list->root_node = current_node->next;
	}

	/* Free node structure */
	payload = dlist_entry_destroy(current_node);

	/* Update count */
	list->node_count--;

	return payload;
}

VOID dlist_destroy(DOUBLE_LIST *list, DOUBLE_LIST_DESTROY_CALLBACK cb)
{
	DOUBLE_LIST_ENTRY *node = list->root_node;
	DOUBLE_LIST_ENTRY *next;
	uint32_t i;

	for (i=0; i<list->node_count; i++) {
		if (node == NULL) {
			/* Corrupted list. This will cause memory leaking. */
			break;
		}

		next = node->next;
		cb(dlist_entry_destroy(node));

		node = next;
	}

	/* Free list structure */
	kfree(list);
	return;
}

DOUBLE_LIST_ENTRY *dlist_entry_create(void* payload)
{
	DOUBLE_LIST_ENTRY* node;

    if(!(node = kmalloc(sizeof(DOUBLE_LIST_ENTRY)))) {
        return NULL;
    }

    /* Populate */
    node->prev 		= NULL;
    node->next 		= NULL;
    node->payload 	= payload;

    return node;
}

VOID *dlist_entry_destroy(DOUBLE_LIST_ENTRY *node)
{
	/* Frees the entry struct and returs pointer to payload */
	VOID *pl = node->payload;

	kfree(node);
	return pl;
}

int32_t dlist_find(DOUBLE_LIST *list, VOID *payload)
{
    /* Iterate through the items */
    DOUBLE_LIST_ENTRY* current_node = list->root_node;
    uint32_t index = 0;

    while (current_node) {
    	if (current_node->payload == payload) {
    		/* Found */
    		return index;
    	}

    	current_node = current_node->next;
    	index++;
    }

    /* Not found */
    return -1;
}

uint32_t dlist_get_count(DOUBLE_LIST *list)
{
	return list->node_count;
}
