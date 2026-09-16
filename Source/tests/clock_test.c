#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/clock_reply.h"

int main(void)
{
    const uint32_t good[8] = {32, 0x80000000u, 0x30047, 8, 0x80000008u, 14, 75000000, 0};
    uint32_t p[8];
    unsigned i;
    assert(clock_reply(good, 0x30047) == 75000000);
    assert(clock_reply(good, 0x30002) == 0);
    for (i = 0; i < 8; ++i) {
        memcpy(p, good, sizeof p);
        p[i] = 0xffffffffu;
        /* Future tag response lengths may exceed the supplied value buffer. */
        assert(clock_reply(p, 0x30047) == (i == 4 ? 75000000u : 0u));
    }
    memcpy(p, good, sizeof p);
    p[4] = 8; assert(clock_reply(p, 0x30047) == 0); /* no response flag */
    p[4] = 0x80000004u; assert(clock_reply(p, 0x30047) == 0); /* short reply */
    p[4] = good[4];
    p[6] = 0; assert(clock_reply(p, 0x30047) == 0);
    p[6] = 340000001; assert(clock_reply(p, 0x30047) == 0);
    p[6] = 75000000; p[2] = 0x30002;
    assert(clock_reply(p, 0x30002) == 75000000);
    puts("PASS: measured/reported clock responses; failed, absent, short and invalid replies rejected.");
    return 0;
}
