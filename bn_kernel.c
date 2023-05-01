#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "bn_kernel.h"

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#ifndef SWAP
#define SWAP(x, y)           \
    do {                     \
        typeof(x) __tmp = x; \
        x = y;               \
        y = __tmp;           \
    } while (0)
#endif

#ifndef DIV_ROUNDUP
#define DIV_ROUNDUP(x, len) (((x) + (len) -1) / (len))
#endif

#define INIT_ALLOC_SIZE 4
#define ALLOC_CHUNK_SIZE 4

/* count leading zeros of src*/
static int bn_clz(const bn *src)
{
    int cnt = 0;
    for (int i = src->size - 1; i >= 0; i--) {
        if (src->number[i]) {
            // prevent undefined behavior when src = 0
            if (BN_WSIZE == 4)
                cnt += __builtin_clz(src->number[i]);
            if (BN_WSIZE == 8)
                cnt += __builtin_clzll(src->number[i]);
            return cnt;
        } else {
            cnt += (BN_WSIZE << 3);
        }
    }
    return cnt;
}

/* count the digits of most significant bit */
static int bn_msb(const bn *src)
{
    return src->size * (BN_WSIZE << 3) - bn_clz(src);
}

/*
 * output bn to decimal string
 * Note: the returned string should be freed with the kfree()
 */
char *bn_to_string(const bn *src)
{
    // log10(x) = log2(x) / log2(10) ~= log2(x) / 3.322
    size_t len = (8 * BN_WSIZE * src->size) / 3 + 2 + src->sign;
    char *s = kmalloc(len, GFP_KERNEL);
    char *p = s;

    memset(s, '0', len - 1);
    s[len - 1] = '\0';

    /* src.number[0] contains least significant bits */
    for (int i = src->size - 1; i >= 0; i--) {
        /* walk through every bit of bn */
        for (bn_data d = 1ul << ((BN_WSIZE << 3) - 1); d; d >>= 1) {
            /* binary -> decimal string */
            int carry = !!(d & src->number[i]);
            for (int j = len - 2; j >= 0; j--) {
                s[j] += s[j] - '0' + carry;
                carry = (s[j] > '9');
                if (carry)
                    s[j] -= 10;
            }
        }
    }
    // skip leading zero
    while (p[0] == '0' && p[1] != '\0') {
        p++;
    }
    if (src->sign)
        *(--p) = '-';
    memmove(s, p, strlen(p) + 1);
    return s;
}

/*
 * alloc a bn structure with the given size
 * the value is initialized to +0
 */
bn *bn_alloc(size_t size)
{
    bn *new = (bn *) kmalloc(sizeof(bn), GFP_KERNEL);
    new->size = size;
    new->capacity = size > INIT_ALLOC_SIZE ? size : INIT_ALLOC_SIZE;
    new->number =
        (bn_data *) kmalloc(sizeof(bn_data) * new->capacity, GFP_KERNEL);
    for (int i = 0; i < size; i++)
        new->number[i] = 0;
    new->sign = 0;
    return new;
}

/*
 * free entire bn data structure
 * return 0 on success, -1 on error
 */
int bn_free(bn *src)
{
    if (src == NULL)
        return -1;
    kfree(src->number);
    kfree(src);
    return 0;
}

/*
 * resize bn
 * return 0 on success, -1 on error
 * data lose IS neglected when shinking the size
 */
static int bn_resize(bn *src, size_t size)
{
    if (!src)
        return -1;
    if (size == src->size)
        return 0;
    if (size == 0)  // prevent krealloc(0) = kfree, which will cause problem
        return bn_free(src);
    if (size > src->capacity) { /* need to allocate larger capacity */
        src->capacity = (size + (ALLOC_CHUNK_SIZE - 1)) &
                        ~(ALLOC_CHUNK_SIZE - 1);  // ceil to 4*n
        src->number =
            krealloc(src->number, sizeof(bn_data) * src->capacity, GFP_KERNEL);
    }
    if (size > src->size) { /* memset(src, 0, size) */
        for (int i = src->size; i < size; i++)
            src->number[i] = 0;
    }
    src->size = size;
    return 0;
}

/*
 * copy the value from src to dest
 * return 0 on success, -1 on error
 */
int bn_cpy(bn *dest, bn *src)
{
    if (bn_resize(dest, src->size) < 0)
        return -1;
    dest->sign = src->sign;
    memcpy(dest->number, src->number, src->size * BN_WSIZE);
    return 0;
}

/* swap bn ptr */
void bn_swap(bn *a, bn *b)
{
    bn tmp = *a;
    *a = *b;
    *b = tmp;
}

/* left bit shift on bn (maximun shift 31) */
void bn_lshift(const bn *src, size_t shift, bn *dest)
{
    size_t z = bn_clz(src);
    shift %= (BN_WSIZE << 3);  // only handle shift within 32 bits atm
    if (!shift)
        return;

    if (shift > z) {
        bn_resize(dest, src->size + 1);
        dest->number[src->size] =
            src->number[src->size - 1] >> ((BN_WSIZE << 3) - shift);
    } else {
        bn_resize(dest, src->size);
    }
    /* bit shift */
    for (int i = src->size - 1; i > 0; i--)
        dest->number[i] = src->number[i] << shift |
                          src->number[i - 1] >> ((BN_WSIZE << 3) - shift);
    dest->number[0] = src->number[0] << shift;
}

/*
 * compare length
 * return 1 if |a| > |b|
 * return -1 if |a| < |b|
 * return 0 if |a| = |b|
 */
int bn_cmp(const bn *a, const bn *b)
{
    if (a->size > b->size) {
        return 1;
    } else if (a->size < b->size) {
        return -1;
    } else {
        for (int i = a->size - 1; i >= 0; i--) {
            if (a->number[i] > b->number[i])
                return 1;
            if (a->number[i] < b->number[i])
                return -1;
        }
        return 0;
    }
}

/* |c| = |a| + |b| */
static void bn_do_add(const bn *a, const bn *b, bn *c)
{
    // max digits = max(sizeof(a) + sizeof(b)) + 1
    int d = MAX(bn_msb(a), bn_msb(b)) + 1;
    d = DIV_ROUNDUP(d, (BN_WSIZE << 3)) + !d;
    bn_resize(c, d);  // round up, min size = 1

    bn_data_tmp carry = 0;
    for (int i = 0; i < c->size; i++) {
        bn_data tmp1 = (i < a->size) ? a->number[i] : 0;
        bn_data tmp2 = (i < b->size) ? b->number[i] : 0;
        carry += (bn_data_tmp) tmp1 + tmp2;
        c->number[i] = carry;
        carry >>= (BN_WSIZE << 3);
    }

    if (!c->number[c->size - 1] && c->size > 1)
        bn_resize(c, c->size - 1);
}

/*
 * |c| = |a| - |b|
 * Note: |a| > |b| must be true
 */
static void bn_do_sub(const bn *a, const bn *b, bn *c)
{
    // max digits = max(sizeof(a) + sizeof(b))
    int d = MAX(a->size, b->size);
    bn_resize(c, d);

    __int128 carry = 0;
    for (int i = 0; i < c->size; i++) {
        bn_data tmp1 = (i < a->size) ? a->number[i] : 0;
        bn_data tmp2 = (i < b->size) ? b->number[i] : 0;

        carry = (__int128) tmp1 - tmp2 - carry;
        if (carry < 0) {
            __int128 temp = 1;
            c->number[i] = carry + (temp << (BN_WSIZE << 3));
            carry = 1;
        } else {
            c->number[i] = carry;
            carry = 0;
        }
    }

    d = bn_clz(c) / (BN_WSIZE << 3);
    if (d == c->size)
        --d;
    bn_resize(c, c->size - d);
}

/*
 * c = a + b
 * Note: work for c == a or c == b
 */
void bn_add(const bn *a, const bn *b, bn *c)
{
    if (a->sign == b->sign) {  // both positive or negative
        bn_do_add(a, b, c);
        c->sign = a->sign;
    } else {          // different sign
        if (a->sign)  // let a > 0, b < 0
            SWAP(a, b);
        int cmp = bn_cmp(a, b);
        if (cmp > 0) {
            /* |a| > |b| and b < 0, hence c = a - |b| */
            bn_do_sub(a, b, c);
            c->sign = 0;
        } else if (cmp < 0) {
            /* |a| < |b| and b < 0, hence c = -(|b| - |a|) */
            bn_do_sub(b, a, c);
            c->sign = 1;
        } else {
            /* |a| == |b| */
            bn_resize(c, 1);
            c->number[0] = 0;
            c->sign = 0;
        }
    }
}

/* c += x, starting from offset */
static void bn_mult_add(bn *c, int offset, bn_data_tmp x)
{
    bn_data_tmp carry = 0;
    bn_data mask = -1;
    for (int i = offset; i < c->size; i++) {
        carry += c->number[i] + (x & mask);
        c->number[i] = carry;
        carry >>= (BN_WSIZE << 3);
        x >>= (BN_WSIZE << 3);
        if (!x && !carry)  // done
            return;
    }
}

/*
 * c = a x b
 * Note: work for c == a or c == b
 * using the simple quadratic-time algorithm (long multiplication)
 */
void bn_mult(const bn *a, const bn *b, bn *c)
{
    // max digits = sizeof(a) + sizeof(b))
    int d = bn_msb(a) + bn_msb(b);
    d = DIV_ROUNDUP(d, (BN_WSIZE << 3)) + !d;  // round up, min size = 1
    bn *tmp;
    /* make it work properly when c == a or c == b */
    if (c == a || c == b) {
        tmp = c;  // save c
        c = bn_alloc(d);
    } else {
        tmp = NULL;
        bn_resize(c, d);
        for (int i = 0; i < d; i++)
            c->number[i] = 0;
    }

    for (int i = 0; i < a->size; i++) {
        for (int j = 0; j < b->size; j++) {
            bn_data_tmp carry = 0;
            carry = (bn_data_tmp) a->number[i] * b->number[j];
            bn_mult_add(c, i + j, carry);
        }
    }
    c->sign = a->sign ^ b->sign;

    if (tmp) {
        bn_cpy(tmp, c);  // restore c
        bn_free(c);
    }
}

/* calc n-th Fibonacci number and save into dest */
void bn_fib(bn *dest, unsigned int n)
{
    bn_resize(dest, 1);
    if (n < 2) {  // Fib(0) = 0, Fib(1) = 1
        dest->number[0] = n;
        return;
    }

    bn *a = bn_alloc(1);
    bn *b = bn_alloc(1);
    dest->number[0] = 1;

    for (unsigned int i = 1; i < n; i++) {
        bn_cpy(b, dest);
        bn_add(dest, a, dest);
        bn_swap(a, b);
    }
    bn_free(a);
    bn_free(b);
}

void bn_fib_fdoubling(bn *dest, unsigned int n)
{
    bn_resize(dest, 1);
    if (n < 2) {  // Fib(0) = 0, Fib(1) = 1
        dest->number[0] = n;
        return;
    }

    bn *f1 = bn_alloc(1);  // f1 = F(k-1)
    bn *f2 = dest;         // f2 = F(k) = dest
    f1->number[0] = 0;
    f2->number[0] = 1;
    bn *k1 = bn_alloc(1);
    bn *k2 = bn_alloc(1);

    bn_data i;
    if (BN_WSIZE == 4)
        i = 1U << ((BN_WSIZE << 3) - 2 - __builtin_clz(n));
    if (BN_WSIZE == 8)
        i = 1U << ((BN_WSIZE << 3) - 2 - __builtin_clzll(n));
    for (; i; i >>= 1) {
        /* F(2k-1) = F(k)^2 + F(k-1)^2 */
        /* F(2k) = F(k) * [ 2 * F(k-1) + F(k) ] */
        bn_lshift(f1, 1, k1);  // k1 = 2 * F(k-1)
        bn_add(k1, f2, k1);    // k1 = 2 * F(k-1) + F(k)
        bn_mult(k1, f2, k2);   // k2 = k1 * f2 = F(2k)
        bn_mult(f2, f2, k1);   // k1 = F(k)^2
        bn_swap(f2, k2);       // f2 <-> k2, f2 = F(2k) now
        bn_mult(f1, f1, k2);   // k2 = F(k-1)^2
        bn_add(k2, k1, f1);    // f1 = k1 + k2 = F(2k-1) now
        if (n & i) {
            bn_swap(f1, f2);     // f1 = F(2k+1)
            bn_add(f1, f2, f2);  // f2 = F(2k+2)
        }
    }
    bn_free(f1);
    bn_free(k1);
    bn_free(k2);
}
