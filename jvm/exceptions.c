/* 
 * This file is part of the Hawkbeans JVM developed by
 * the HExSA Lab at Illinois Institute of Technology.
 *
 * Copyright (c) 2019, Kyle C. Hale <khale@cs.iit.edu>
 *
 * All rights reserved.
 *
 * Author: Kyle C. Hale <khale@cs.iit.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the 
 * file "LICENSE.txt".
 */
#include <stdlib.h>
#include <string.h>

#include <types.h>
#include <class.h>
#include <stack.h>
#include <mm.h>
#include <thread.h>
#include <exceptions.h>
#include <bc_interp.h>
#include <gc.h>

extern jthread_t * cur_thread;

/* 
 * Maps internal exception identifiers to fully
 * qualified class paths for the exception classes.
 * Note that the ones without fully qualified paths
 * will not be properly raised. 
 *
 * TODO: add the classes for these
 *
 */
static const char * excp_strs[16] __attribute__((used)) =
{
	"java/lang/NullPointerException",
	"java/lang/IndexOutOfBoundsException",
	"java/lang/ArrayIndexOutOfBoundsException",
	"IncompatibleClassChangeError",
	"java/lang/NegativeArraySizeException",
	"java/lang/OutOfMemoryError",
	"java/lang/ClassNotFoundException",
	"java/lang/ArithmeticException",
	"java/lang/NoSuchFieldError",
	"java/lang/NoSuchMethodError",
	"java/lang/RuntimeException",
	"java/io/IOException",
	"FileNotFoundException",
	"java/lang/InterruptedException",
	"java/lang/NumberFormatException",
	"java/lang/StringIndexOutOfBoundsException",
};

int 
hb_excp_str_to_type (char * str)
{
    for (int i = 0; i < sizeof(excp_strs)/sizeof(char*); i++) {
        if (strstr(excp_strs[i], str))
                return i;
    }
    return -1;
}

/**
 * @brief create an object base on java lib
 * 
 * @param class_nm 
 * @return obj_ref_t* 
 */
obj_ref_t*
hb_create_obj(char* class_nm) 
{
	obj_ref_t * ref = NULL;
	native_obj_t * obj = NULL;
	java_class_t * cls = hb_get_or_load_class(class_nm);
	cls = hb_resolve_class(cls->this, cls);

	if (!cls) { 
		HB_ERR("Could not get throwable class\n");
		return NULL;
	}
	ref = gc_obj_alloc(cls);
	if (!ref) {
		HB_ERR("Could not allocate throwable object\n");
		return NULL;
	}
	return ref;
}

/*
 * Throws an exception given an internal ID
 * that refers to an exception type. This is to 
 * be used by the runtime (there is no existing
 * exception object, so we have to create a new one
 * and init it).
 *
 * @return: none. exits on failure.
 *
 */
// WRITE ME
void
hb_throw_and_create_excp (u1 type)
{
    char * class_nm = (char *)excp_strs[type];
	obj_ref_t *ref = hb_create_obj(class_nm);
	hb_invoke_ctor(ref);
	hb_throw_exception(ref);
}


/* 
 * gets the exception message from the object 
 * ref referring to the exception object.
 *
 * NOTE: caller must free the string
 *
 */
static char *
get_excp_str (obj_ref_t * eref)
{
	char * ret;
	native_obj_t * obj = (native_obj_t*)eref->heap_ptr;
		
	obj_ref_t * str_ref = obj->fields[0].obj;
	native_obj_t * str_obj;
	obj_ref_t * arr_ref;
	native_obj_t * arr_obj;
	int i;
	
	if (!str_ref) {
		return NULL;
	}

	str_obj = (native_obj_t*)str_ref->heap_ptr;
	
	arr_ref = str_obj->fields[0].obj;

	if (!arr_ref) {
		return NULL;
	}

	arr_obj = (native_obj_t*)arr_ref->heap_ptr;

	ret = malloc(arr_obj->flags.array.length+1);

	for (i = 0; i < arr_obj->flags.array.length; i++) {
		ret[i] = arr_obj->fields[i].char_val;
	}

	ret[i] = 0;

	return ret;
}

/**
 * @brief confirm current status if it is in this item of exception table
 * 
 * @param t 
 * @param cls 
 * @param curpc 
 * @return int 
 */
int
check_catchtype_and_class(excp_table_t *t, java_class_t *cls, u2 curpc) 
{
	java_class_t *super;
	const char * tname;
	const char * cname;

	if (curpc >= t->start_pc && curpc < t->end_pc) {

		CONSTANT_Class_info_t *c = (CONSTANT_Class_info_t*)cur_thread->cur_frame->cls->const_pool[t->catch_type];
		tname = (char*)hb_get_const_str(c->name_idx, cur_thread->cur_frame->cls);
		cname = hb_get_class_name(cls);

		if (t->catch_type == 0 || strcmp(cname, tname) == 0) {
			return 1;
		}
		
		super = hb_get_super_class(cls);
		cname = hb_get_class_name(super);
		while(super) {
			if (strcmp(cname, tname) == 0) {
				return 1;
			}
			super = hb_get_super_class(cls);
			cname = hb_get_class_name(super);
		}
	}
	return 0;
}

/**
 * @brief 
 * 
 * @param cls  error cls
 * @return int 
 */
int
find_exception_table(java_class_t *cls) 
{
	stack_frame_t *f;
	excp_table_t *t;
	u2 curpc = cur_thread->cur_frame->pc;

	int excplen;
	int i;

	while(cur_thread->cur_frame) {
		f = cur_thread->cur_frame;

		excplen = f->minfo->code_attr->excp_table_len;
		t = f->minfo->code_attr->excp_table;

		if (excplen > 0) {
			for (i = 0; i < excplen; i++) {
				if (check_catchtype_and_class(t, cls, curpc)) {
					f->pc = t->handler_pc;
					return 1;
				}
			}
		}
		hb_pop_frame(cur_thread);
	}
	return 0;
}

/*
 * Throws an exception using an
 * object reference to some exception object (which
 * implements Throwable). To be used with athrow.
 * If we're given a bad reference, we throw a 
 * NullPointerException.
 *
 * @return: none. exits on failure.  
 *
 */
void
hb_throw_exception (obj_ref_t * eref)
{
	native_obj_t *obj;
	java_class_t *cls;
	var_t v;

	obj = (native_obj_t*)eref->heap_ptr;
	cls = (java_class_t*)obj->class;

	if (obj == NULL) {
		hb_throw_and_create_excp(EXCP_NULL_PTR);
		return;
	}
	if (find_exception_table(cls)) {
		// HB_DEBUG("find exception pc, %i", cur_thread->cur_frame->pc);
		cur_thread->cur_frame->op_stack->oprs[0].obj = eref;
		cur_thread->cur_frame->op_stack->sp = 1;
		return;
	}
	HB_INFO("Exception in thread \"%s\" %s: %s", cur_thread->name, obj->class->name, get_excp_str(eref));
	exit(EXIT_FAILURE);
}
