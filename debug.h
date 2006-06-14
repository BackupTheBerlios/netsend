/*
 * $Id$
 */

#ifndef NETSEND_DEBUG_HDR
#define NETSEND_DEBUG_HDR

#ifdef DEBUG
#include <stdio.h>
#define DEBUGPRINTF( fmt, ... )  fprintf(stderr, "DEBUG: %s:%u - " fmt,  __FILE__, __LINE__, __VA_ARGS__)
#else
#define DEBUGPRINTF( fmt, ... )  do { } while(0)
#define NDEBUG
#endif

#include <assert.h>

#endif /* NETSEND_DEBUG_HDR */
