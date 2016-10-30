/*
 * henjin_object.h
 *
 *	Henjin base object.
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_OBJECT_H_
#define SUBSYSTEMS_HENJIN_OBJECT_H_

#include "henjin.h"

/*
 * Casting macros
 */
#define HJ_CAST(object, class, new_obj) (new_obj*)(((HJ_OBJECT*)object)->inst_table[class])
#define HJ_SUPPORTS(object, class) 		(((HJ_OBJECT*)object)->inst_table[class] != NULL ? TRUE : FALSE)

/*
 * Classes
 */
typedef enum {
	HJ_CLASS_OBJECT,
	HJ_CLASS_CONTROL,
	HJ_CLASS_DESKTOP,
	HJ_CLASS_TASKPANEL,
	HJ_CLASS_WINDOW,
	HJ_CLASS_BUTTON,
	HJ_CLASS_MAX
} HJ_CLASS;

/*
 * In the pseudo-object-oriented approach that we use
 * the base object (HJ_OBJECT) will be embedded in all
 * other objects.
 *
 * Instance table is used to cast the object to
 * a successor class.
 */
typedef struct HJ_OBJECT HJ_OBJECT;
struct HJ_OBJECT {
	/* Class */
	HJ_CLASS class;

	/* Size of the whole structure in bytes */
	uint32_t size;

	/* Used to cast object to other classes */
	void *inst_table[HJ_CLASS_MAX];
};

#endif /* SUBSYSTEMS_HENJIN_OBJECT_H_ */
