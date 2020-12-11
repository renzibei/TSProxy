#include "constants.h"

#include <uuid/uuid.h>
#include <string.h>

static int hasParsedKey = 0;
static uuid_t storedId;

namespace constants {

int getKey(char (&dst)[16]) {
    // uuid_t id;
    if (hasParsedKey == 0) {
        int code = uuid_parse(constants::uuidKey, storedId);
        if (code != 0)
            return code;
        hasParsedKey = 1;
    }
    memcpy(dst, storedId, 16);
    return 0;
}

}