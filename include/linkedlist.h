/*
 * linkedlist.h
 *
 *	Doubly-linked list container intended for kernel mode and user mode usage.
 *
 *	Of course using doubly-linked list instead of singly-linked one is useless
 *	at this point.
 *
 *	But anyway.
 *
 *	TODO:
 *		- Add single list support too.
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Admin
 */

#ifndef LIBC_LINKEDLIST_H_
#define LIBC_LINKEDLIST_H_

#include <types.h>

/*
 * List node.
 */
typedef struct DOUBLE_LIST_ENTRY DOUBLE_LIST_ENTRY;
struct DOUBLE_LIST_ENTRY {
	DOUBLE_LIST_ENTRY	*prev;
	DOUBLE_LIST_ENTRY	*next;
    VOID				*payload;
};

/*
 * List
 */
typedef struct DOUBLE_LIST DOUBLE_LIST;
struct DOUBLE_LIST {
    uint32_t 			node_count;
    DOUBLE_LIST_ENTRY	*root_node;
};

/*
 * This callback is invoked by list destructors, to return
 * payload pointers for further destruction to user.
 */
typedef VOID __callback (*DOUBLE_LIST_DESTROY_CALLBACK)(void *payload);

/*
 * Routines
 */
DOUBLE_LIST *dlist_create();
VOID		dlist_destroy(DOUBLE_LIST *list, DOUBLE_LIST_DESTROY_CALLBACK cb);
uint32_t 	dlist_add(DOUBLE_LIST *list, VOID *payload);
int32_t		dlist_find(DOUBLE_LIST *list, VOID *payload);
uint32_t	dlist_get_count(DOUBLE_LIST *list);
VOID 		*dlist_get_at(DOUBLE_LIST* list, uint32_t index);
VOID 		*dlist_remove_at(DOUBLE_LIST* list, uint32_t index);

DOUBLE_LIST_ENTRY *dlist_entry_create(void* payload);
VOID			  *dlist_entry_destroy(DOUBLE_LIST_ENTRY *node);

#endif /* LIBC_LINKEDLIST_H_ */
