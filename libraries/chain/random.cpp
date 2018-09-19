#include <eosio/chain/random.hpp>

#define	ULONG_MAX  ((unsigned long)(~0L))     /* 0xFFFFFFFF for 32-bits */
#define	LONG_MAX   ((long)(ULONG_MAX >> 1))   /* 0x7FFFFFFF for 32-bits*/

/* x**31 + x**3 + 1.  */
#define	TYPE_3		3
#define	BREAK_3		128
#define	DEG_3		31
#define	SEP_3		3

#define RANDTBL_INIT TYPE_3, \
0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342, \
0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb, \
0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd, \
0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86, \
0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7, \
0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc, \
0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b, \
0xf5ad9d0e, 0x8999220b, 0x27fb47b9

namespace eosio { namespace chain {

/* static long int randtbl_init[DEG_3 + 1] =
{ TYPE_3,
  0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342,
  0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb,
  0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd,
  0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86,
  0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7,
  0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc,
  0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b,
  0xf5ad9d0e, 0x8999220b, 0x27fb47b9
}; */

static int rand_type = TYPE_3;
static int rand_deg = DEG_3;
static int rand_sep = SEP_3;


class random_impl {
public:
   random_impl( uint32_t seed );
   int operator()();

private:
   long int randtbl[DEG_3 + 1];

   long int *fptr;
   long int *rptr;

   long int *state;

   long int *end_ptr;
};

random_impl::random_impl( uint32_t seed )
   : randtbl{RANDTBL_INIT},
     fptr(&randtbl[SEP_3 + 1]),
     rptr(&randtbl[1]),
     state(&randtbl[1]),
     end_ptr(&randtbl[sizeof(randtbl) / sizeof(randtbl[0])])
{
   // std::copy(std::begin(randtbl_init), std::end(randtbl_init), std::begin(randtbl));

    state[0] = seed;
    register long int i;
    for (i = 1; i < rand_deg; ++i)
        state[i] = (1103515145 * state[i - 1]) + 12345;
    fptr = &state[rand_sep];
    rptr = &state[0];
    for (i = 0; i < 10 * rand_deg; ++i) {
        this->operator()();
    }
}

int random_impl::operator()() {
    long int i;
    *fptr += *rptr;
    /* Chucking least random bit.  */
    i = (*fptr >> 1) & LONG_MAX;
    ++fptr;
    if (fptr >= end_ptr)
    {
        fptr = state;
        ++rptr;
    }
    else
    {
        ++rptr;
        if (rptr >= end_ptr)
            rptr = state;
    }
    return static_cast<int>(i);
}

random::random( uint32_t seed ) : impl(std::make_unique<random_impl>(seed)) {
}

int random::operator()() {
   return impl->operator()();
}

} } // namespace eosio::chain
