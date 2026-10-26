// Look file tool.
//   lumina-look export <dir>      write the built-in filters and styles as look files (templates for designers)
//   lumina-look check <file>...   parse look files and report errors
//   lumina-look verify <dir>      check that the exported built-ins in <dir> read back identical to the built-ins
#include <cstdio>
#include <string>
#include "LmLook.hpp"

namespace {

const char* kFilterIds[] = {"sunny", "film", "oxygen", "bellflower", "fair", "gentle", "first-sight", "gray",
                            "island", "summer", "clear", "icy", "natural", "glow", "porcelain"};
const char* kStyleIds[] = {"fair", "korean", "energetic", "warm", "western", "elegant",
                           "sweet", "deep", "bare-face", "senior", "junior"};
const int kFilters = sizeof(kFilterIds) / sizeof(kFilterIds[0]);
const int kStyles = sizeof(kStyleIds) / sizeof(kStyleIds[0]);

std::string FilterPath(const std::string& dir, int i) {
    return dir + "/filter-" + kFilterIds[i] + ".json";
}

std::string StylePath(const std::string& dir, int i) {
    return dir + "/style-" + kStyleIds[i] + ".json";
}

bool Write(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

bool Same(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) {  // 用 != 而不是 memcmp：-0.0 与 0.0 效果相同
            return false;
        }
    }
    return true;
}

bool SameGrade(const LmColorGrade& a, const LmColorGrade& b) {
    return a.temperature == b.temperature && a.tint == b.tint && a.saturation == b.saturation &&
           a.contrast == b.contrast && a.brightness == b.brightness && a.fade == b.fade && a.skin == b.skin &&
           Same(a.shadowTint, b.shadowTint, 3) && Same(a.highlightTint, b.highlightTint, 3);
}

bool SameMakeup(const LmMakeupLook& a, const LmMakeupLook& b) {
    return Same(a.lip, b.lip, 3) && a.lipOpacity == b.lipOpacity && Same(a.blush, b.blush, 3) &&
           a.blushOpacity == b.blushOpacity && a.blushSize == b.blushSize && Same(a.brow, b.brow, 3) &&
           a.browOpacity == b.browOpacity && Same(a.shadow, b.shadow, 3) && a.shadowOpacity == b.shadowOpacity &&
           Same(a.contour, b.contour, 3) && a.contourOpacity == b.contourOpacity && Same(a.liner, b.liner, 3) &&
           a.linerOpacity == b.linerOpacity && Same(a.lash, b.lash, 3) && a.lashOpacity == b.lashOpacity &&
           a.foundation == b.foundation;
}

bool Verify(const std::string& path, const LmLook& builtin) {
    LmLook look;
    std::string error;
    if (!LmLoadLookFile(path.c_str(), look, error)) {
        fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    const bool ok = look.name == builtin.name && look.hasGrade == builtin.hasGrade &&
                    look.hasMakeup == builtin.hasMakeup && (!look.hasGrade || SameGrade(look.grade, builtin.grade)) &&
                    (!look.hasMakeup || SameMakeup(look.makeup, builtin.makeup));
    if (!ok) {
        fprintf(stderr, "%s differs from the built-in look\n", path.c_str());
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s export <dir> | check <file>... | verify <dir>\n", argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    const std::string dir = argv[2];
    bool ok = true;
    if (cmd == "export") {
        for (int i = 0; i < kFilters; ++i) {
            ok = Write(FilterPath(dir, i), LmLookToJson(*LmBuiltinFilter(i + 1))) && ok;
        }
        for (int i = 0; i < kStyles; ++i) {
            ok = Write(StylePath(dir, i), LmLookToJson(*LmBuiltinStyle(i + 1))) && ok;
        }
    } else if (cmd == "verify") {
        for (int i = 0; i < kFilters; ++i) {
            ok = Verify(FilterPath(dir, i), *LmBuiltinFilter(i + 1)) && ok;
        }
        for (int i = 0; i < kStyles; ++i) {
            ok = Verify(StylePath(dir, i), *LmBuiltinStyle(i + 1)) && ok;
        }
    } else if (cmd == "check") {
        for (int i = 2; i < argc; ++i) {
            LmLook look;
            std::string error;
            if (LmLoadLookFile(argv[i], look, error)) {
                printf("%s: ok (%s%s%s)\n", argv[i], look.name.c_str(), look.hasGrade ? ", color" : "",
                       look.hasMakeup ? ", makeup" : "");
            } else {
                printf("%s: %s\n", argv[i], error.c_str());
                ok = false;
            }
        }
    } else {
        fprintf(stderr, "unknown command %s\n", cmd.c_str());
        return 2;
    }
    if (ok && cmd != "check") {
        printf("%s: %d filters, %d styles ok\n", cmd.c_str(), kFilters, kStyles);
    }
    return ok ? 0 : 1;
}
