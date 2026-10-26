#include "LmParams.hpp"

#include "LmLook.hpp"

const LmLook* LmActiveStyle(const LmParams& params) {
    return params.look ? params.look.get() : LmBuiltinStyle(params.styleType);
}
