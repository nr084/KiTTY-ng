#include "putty.h"

/*
 * We need this to link with the RSA code, because rsa_ssh1_encrypt()
 * pads its data with random bytes. Since we only use rsa_ssh1_decrypt()
 * and the signing functions, which are deterministic, this should
 * never be called.
 *
 * If it _is_ called, there is a _serious_ problem, because it
 * won't generate true random numbers. So we must scream, panic,
 * and exit immediately if that should happen.
 */
void random_read(void *buf, size_t size)
{
    modalfatalbox("Internal error: attempt to use random numbers in Pageant");
}

