// macOS：用 ImageIO 解码图片为 RGBA8
#include "LmImageLoader.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include "LmLog.hpp"

bool LmLoadImageRGBA(const std::string& path, int maxSide, std::vector<uint8_t>& rgba, int& width, int& height) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
    if (!url) {
        return false;
    }
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source) {
        LM_LOG_E << "Failed to open image " << path;
        return false;
    }
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image) {
        LM_LOG_E << "Failed to decode image " << path;
        return false;
    }

    const int srcW = static_cast<int>(CGImageGetWidth(image));
    const int srcH = static_cast<int>(CGImageGetHeight(image));
    const float scale = std::min(1.0f, static_cast<float>(maxSide) / std::max(srcW, srcH));
    width = std::max(1, static_cast<int>(srcW * scale));
    height = std::max(1, static_cast<int>(srcH * scale));
    rgba.assign(static_cast<size_t>(width) * height * 4, 0);

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), width, height, 8, static_cast<size_t>(width) * 4, space,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!ctx) {
        CGImageRelease(image);
        rgba.clear();
        return false;
    }
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    // 位图上下文原点在左下角，绘制结果按内存顺序就是从上到下的行
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
    CGContextRelease(ctx);
    CGImageRelease(image);
    return true;
}
