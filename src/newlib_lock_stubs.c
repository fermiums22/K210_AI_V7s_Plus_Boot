/* Minimal newlib/pthread lock stubs for bootloader milestone.
 *
 * The boot image is a single-purpose updater/chainloader.  We only need enough
 * libc support for printf/FatFs/Kendryte wrappers.  Real multi-threaded POSIX
 * cancellation/recursive-lock behaviour is intentionally not implemented here.
 */

#include <stdint.h>

#ifndef PTHREAD_CANCEL_DISABLE
#define PTHREAD_CANCEL_DISABLE 0
#endif

int pthread_setcancelstate(int state, int *oldstate)
{
    if (oldstate)
        *oldstate = PTHREAD_CANCEL_DISABLE;
    (void)state;
    return 0;
}

void _lock_init(void *lock)                    { (void)lock; }
void _lock_init_recursive(void *lock)          { (void)lock; }
void _lock_close(void *lock)                   { (void)lock; }
void _lock_close_recursive(void *lock)         { (void)lock; }
void _lock_acquire(void *lock)                 { (void)lock; }
void _lock_acquire_recursive(void *lock)       { (void)lock; }
int  _lock_try_acquire(void *lock)             { (void)lock; return 0; }
int  _lock_try_acquire_recursive(void *lock)   { (void)lock; return 0; }
void _lock_release(void *lock)                 { (void)lock; }
void _lock_release_recursive(void *lock)       { (void)lock; }
