#include "types.h"

int main(void)
{
    const char *end;
    char buf[100];
    double n;
    n = joe_strtod("0xffffffffffffffff", &end);
    if (*end || n != 18446744073709551616.0) return 1;
    n = joe_strtod("0x8000000000000000", &end);
    if (*end || n != 9223372036854775808.0) return 2;
    n = joe_strtod("0x100000001", &end);
    if (*end || n != 4294967297.0) return 3;
    n = joe_strtod("0b1111_0000", &end);
    if (*end || n != 240.0) return 4;
    n = joe_strtod("0o17", &end);
    if (*end || n != 15.0) return 5;
    joe_snprintf_1(buf, sizeof(buf), "%" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)4294967297.0);
    if (strcmp(buf, "4294967297")) return 6;
    printf("PASS calculator unsigned 64-bit conversion, binary/octal parsing, and CRT I64 formatting\n");
    return 0;
}
