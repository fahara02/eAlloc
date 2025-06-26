/**
 * @file builtins.h
 * @brief Portable bit manipulation intrinsics (ffs/fls) with architecture-specific optimizations.
 *
 * Provides fast find-first-set (ffs) and find-last-set (fls) implementations for GCC, MSVC, ARM,
 * and generic fallback.
 *
 * - Use dsa_decl ffs(unsigned int) and fls(unsigned int) for portable, inlined bit scanning.
 * - Selects the best implementation for the detected compiler/architecture.
 */
#pragma once
#if defined(__cplusplus)
    #define dsa_decl inline
#else
    #define dsa_decl static
#endif

/**
 * @defgroup bitmanip Bit Manipulation Intrinsics
 * @{
 */

/**
 * @brief Find the first set bit in a word.
 *
 * Returns the position of the first set bit (1-based index) in the given word.
 * If the word is zero, returns -1.
 *
 * @param word The input word to scan.
 * @return The position of the first set bit, or -1 if the word is zero.
 */
dsa_decl int ffs(unsigned int word);

/**
 * @brief Find the last set bit in a word.
 *
 * Returns the position of the last set bit (1-based index) in the given word.
 * If the word is zero, returns -1.
 *
 * @param word The input word to scan.
 * @return The position of the last set bit, or -1 if the word is zero.
 */
dsa_decl int fls(unsigned int word);

/*
** gcc 3.4 and above have builtin support, specialized for architecture.
** Some compilers masquerade as gcc; patchlevel test filters them out.
*/
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) \
    && defined(__GNUC_PATCHLEVEL__)

/**
 * @brief GCC-specific implementation of ffs.
 *
 * Uses the __builtin_ffs intrinsic for GCC 3.4 and above.
 */
dsa_decl int ffs(unsigned int word) { return __builtin_ffs(word) - 1; }

dsa_decl int fls(unsigned int word)
{
    const int bit = word ? 32 - __builtin_clz(word) : 0;
    return bit - 1;
}

#elif defined(_MSC_VER) && (_MSC_VER >= 1400) && (defined(_M_IX86) || defined(_M_X64))
/* Microsoft Visual C++ support on x86/X64 architectures. */

    #include <intrin.h>

    #pragma intrinsic(_BitScanReverse)
    #pragma intrinsic(_BitScanForward)

dsa_decl int fls(unsigned int word)
{
    unsigned long index;
    return _BitScanReverse(&index, word) ? index : -1;
}

dsa_decl int ffs(unsigned int word)
{
    unsigned long index;
    return _BitScanForward(&index, word) ? index : -1;
}

#elif defined(_MSC_VER) && defined(_M_PPC)
/* Microsoft Visual C++ support on PowerPC architectures. */

    #include <ppcintrinsics.h>

dsa_decl int fls(unsigned int word)
{
    const int bit = 32 - _CountLeadingZeros(word);
    return bit - 1;
}

dsa_decl int ffs(unsigned int word)
{
    const unsigned int reverse = word & (~word + 1);
    const int bit = 32 - _CountLeadingZeros(reverse);
    return bit - 1;
}

#elif defined(__ARMCC_VERSION)
/* RealView Compilation Tools for ARM */

dsa_decl int ffs(unsigned int word)
{
    const unsigned int reverse = word & (~word + 1);
    const int bit = 32 - __clz(reverse);
    return bit - 1;
}

dsa_decl int fls(unsigned int word)
{
    const int bit = word ? 32 - __clz(word) : 0;
    return bit - 1;
}

#else
/* Fall back to generic implementation. */

dsa_decl int fls_generic(unsigned int word)
{
    int bit = 32;

    if(!word) bit -= 1;
    if(!(word & 0xffff0000))
    {
        word <<= 16;
        bit -= 16;
    }
    if(!(word & 0xff000000))
    {
        word <<= 8;
        bit -= 8;
    }
    if(!(word & 0xf0000000))
    {
        word <<= 4;
        bit -= 4;
    }
    if(!(word & 0xc0000000))
    {
        word <<= 2;
        bit -= 2;
    }
    if(!(word & 0x80000000))
    {
        word <<= 1;
        bit -= 1;
    }

    return bit;
}

/* Implement ffs in terms of fls. */
dsa_decl int ffs(unsigned int word) { return fls_generic(word & (~word + 1)) - 1; }

dsa_decl int fls(unsigned int word) { return fls_generic(word) - 1; }
/** @} */
#endif

#undef dsa_decl