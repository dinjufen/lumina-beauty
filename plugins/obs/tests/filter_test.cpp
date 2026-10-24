// Test of the OBS filter logic without the OBS window or video thread: compiles the plugin source into
// this program and calls its create / filter_video / destroy callbacks directly with I420, NV12 and YUY2
// frames, then checks the frames were processed in place and kept their format.
//
// Run from the OBS bin directory (obs.dll is needed for obs_data and logging). Set LUMINA_MODEL_DIR to the
// models directory to include face effects.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../lumina-filter.cpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const int kW = 640;
const int kH = 480;

void FillPattern(std::vector<uint8_t>& y, std::vector<uint8_t>& u, std::vector<uint8_t>& v) {
    y.resize(kW * kH);
    u.resize(kW / 2 * kH / 2);
    v.resize(kW / 2 * kH / 2);
    for (int r = 0; r < kH; ++r)
        for (int c = 0; c < kW; ++c)
            y[r * kW + c] = static_cast<uint8_t>(40 + 150 * c / kW + 20 * std::sin(r * 0.3));
    for (int r = 0; r < kH / 2; ++r)
        for (int c = 0; c < kW / 2; ++c) {
            u[r * (kW / 2) + c] = static_cast<uint8_t>(110 + 20 * r / (kH / 2));
            v[r * (kW / 2) + c] = static_cast<uint8_t>(140 + 20 * c / (kW / 2));
        }
}

double MeanAbsDiff(const uint8_t* a, const uint8_t* b, size_t n) {
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += std::abs(int(a[i]) - int(b[i]));
    return s / n;
}

bool RunFormat(void* filter, video_format format, video_colorspace cs, video_range_type range) {
    std::vector<uint8_t> y, u, v, uv, packed;
    FillPattern(y, u, v);
    obs_source_frame frame = {};
    frame.width = kW;
    frame.height = kH;
    frame.format = format;
    frame.full_range = range == VIDEO_RANGE_FULL;
    video_format_get_parameters_for_format(cs, range, format, frame.color_matrix, frame.color_range_min,
                                           frame.color_range_max);
    std::vector<uint8_t> before;
    if (format == VIDEO_FORMAT_I420) {
        frame.data[0] = y.data(); frame.linesize[0] = kW;
        frame.data[1] = u.data(); frame.linesize[1] = kW / 2;
        frame.data[2] = v.data(); frame.linesize[2] = kW / 2;
        before = y;
    } else if (format == VIDEO_FORMAT_NV12) {
        uv.resize(kW * kH / 2);
        for (size_t i = 0; i < u.size(); ++i) { uv[i * 2] = u[i]; uv[i * 2 + 1] = v[i]; }
        frame.data[0] = y.data(); frame.linesize[0] = kW;
        frame.data[1] = uv.data(); frame.linesize[1] = kW;
        before = y;
    } else {
        packed.resize(kW * kH * 2);
        for (int r = 0; r < kH; ++r)
            for (int c = 0; c < kW; c += 2) {
                uint8_t* p = &packed[(r * kW + c) * 2];
                const int ci = (r / 2) * (kW / 2) + c / 2;
                p[0] = y[r * kW + c]; p[1] = u[ci]; p[2] = y[r * kW + c + 1]; p[3] = v[ci];
            }
        frame.data[0] = packed.data(); frame.linesize[0] = kW * 2;
        before = packed;
    }
    obs_source_frame* out = FilterVideo(filter, &frame);
    const double diff = MeanAbsDiff(out->data[0], before.data(), before.size());
    const bool ok = out == &frame && out->format == format && diff > 0.5 && diff < 60.0;
    printf("  format %d, %s, %s range: mean change %.2f -> %s\n", int(format), cs == VIDEO_CS_709 ? "709" : "601",
           frame.full_range ? "full" : "limited", diff, ok ? "ok" : "FAILED");
    return ok;
}

} // namespace

int main() {
    if (!obs_startup("en-US", nullptr, nullptr)) {
        printf("obs_startup failed\n");
        return 1;
    }
    obs_data_t* settings = obs_data_create();
    Defaults(settings);
    obs_data_set_int(settings, "smooth", 100);
    obs_data_set_int(settings, "whiten", 100);
    obs_data_set_int(settings, "filter", 3);
    obs_data_set_int(settings, "filter_intensity", 100);
    void* filter = Create(settings, nullptr);

    bool ok = true;
    ok = RunFormat(filter, VIDEO_FORMAT_I420, VIDEO_CS_601, VIDEO_RANGE_FULL) && ok;
    ok = RunFormat(filter, VIDEO_FORMAT_NV12, VIDEO_CS_709, VIDEO_RANGE_PARTIAL) && ok;
    ok = RunFormat(filter, VIDEO_FORMAT_YUY2, VIDEO_CS_709, VIDEO_RANGE_PARTIAL) && ok;
    ok = RunFormat(filter, VIDEO_FORMAT_UYVY, VIDEO_CS_601, VIDEO_RANGE_FULL) && ok;

    // 不支持的格式应原样放过
    obs_source_frame bgra = {};
    bgra.width = kW;
    bgra.height = kH;
    bgra.format = VIDEO_FORMAT_BGRA;
    std::vector<uint8_t> pixels(kW * kH * 4, 77);
    bgra.data[0] = pixels.data();
    bgra.linesize[0] = kW * 4;
    FilterVideo(filter, &bgra);
    const bool untouched = pixels[12345] == 77;
    printf("  unsupported format passes through: %s\n", untouched ? "ok" : "FAILED");
    ok = ok && untouched;

    Destroy(filter);
    obs_data_release(settings);
    obs_shutdown();
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
