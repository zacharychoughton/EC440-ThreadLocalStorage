#ifndef tls_h
#define tls_h 

#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h> 
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

int tls_create(unsigned int size); 
/*The tls_create() creates a local storage area (LSA) for the currently-executing thread. This
LSA must hold at least size bytes.
This function returns 0 on success, and -1 for errors. It is an error if a thread already has more
than 0 bytes of local storage.
*/

int tls_destroy(); 
/*The tls_destroy() function frees a previously-allocated LSA of the current thread. 
It returns 0 on success and -1 when an error occurs. 
It is an error if the thread does not have an LSA.
*/

int tls_read(unsigned int offset, unsigned int length, char *buffer);
/*The tls_read() function reads data from the current thread’s local storage. 
The read data must be copied to bytes starting at buffer. 
The data is read from a location in the local storage area bytes from the start of the LSA.

This function returns 0 on success and -1 when an error occurs.
It is an error if the function is asked to read more data than the LSA can hold 
(i.e., offset+length>size of LSA) or if the current thread has no LSA.
*/

int tls_write(unsigned int offset, unsigned int length, const char *buffer);
/* The tls_write() writes data to the current thread’s local storage. 
The written data comes from bytes starting at buffer. 
The data is written to a location in the local storage area bytes from the start of the LSA.
This function returns 0 on success and -1 when an error occurs. 

It is an error if the function is asked to write more data than the LSA can hold 
(i.e., offset+length>size of LSA) or if the current thread has no LSA.
This function can assume that buffer holds at least length bytes. If not, the result of the call is undefined.
*/

int tls_clone(pthread_t tid);
/*
The tls_clone() function clones the local storage area of a target thread, tid. 
When a LSA is cloned, the content is not simply copied. 
Instead, the storage areas of both threads initially refer to the same memory location. 
When one thread writes to its own LSA (using tls_write), the TLS library creates a private copy of the region that is written. 
Note that the remaining unmodified regions of the TLS remain shared. 
This approach is called copy on write (CoW), and saves memory space by avoiding unnecessary copy operations.

The function returns 0 on success, and -1 when an error occurs. 
It is an error when the target thread has no LSA, or when the current thread already has an LSA.
Whenever a thread attempts to read from or write to any LSA (even its own) without using the appropriate tls_read and tls_write functions, 
then this thread should be terminated (by calling pthread_exit). Remaining threads should continue to run unaffected.
*/

#endif 