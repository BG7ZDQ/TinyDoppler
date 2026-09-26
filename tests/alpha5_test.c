#include "sgp4.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

int main(void)
{
    char line1[] =
        "1 25544U 98067A   04236.56031392  .00020137  00000-0  16538-3 0  9993";
    char line2[] =
        "2 25544  51.6335 344.7760 0007976 126.2523 325.9359 15.70406856328906";
    assert(strlen(line1) == 69 && strlen(line2) == 69);
    memcpy(line1 + 2, "A0465", 5);
    memcpy(line2 + 2, "A0465", 5);
    line1[68] = (char)('0' + sgp4_calculate_checksum(line1));
    line2[68] = (char)('0' + sgp4_calculate_checksum(line2));

    sgp4_tle_t tle;
    assert(sgp4_parse_tle_2line(line1, line2, &tle) == SGP4_SUCCESS);
    assert(tle.norad_id == 100465);

    memcpy(line2 + 2, "A0466", 5);
    line2[68] = (char)('0' + sgp4_calculate_checksum(line2));
    assert(sgp4_parse_tle_2line(line1, line2, &tle) ==
           SGP4_ERROR_INVALID_TLE_FORMAT);
    memcpy(line2 + 2, "A0465", 5);
    line2[68] = (char)('0' + sgp4_calculate_checksum(line2));

    memcpy(line1 + 2, "I0465", 5);
    memcpy(line2 + 2, "I0465", 5);
    line1[68] = (char)('0' + sgp4_calculate_checksum(line1));
    line2[68] = (char)('0' + sgp4_calculate_checksum(line2));
    assert(sgp4_parse_tle_2line(line1, line2, &tle) ==
           SGP4_ERROR_INVALID_TLE_FORMAT);
    return 0;
}
