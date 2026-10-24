#ifndef LUMINA_IMAGE_LOADER_HPP
#define LUMINA_IMAGE_LOADER_HPP

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 解码一张图片为 RGBA8
 * @param[in]  path    图片路径，UTF-8
 * @param[in]  maxSide 最长边上限，超过时等比缩小
 * @param[out] rgba    像素数据，逐行从上到下，每像素 4 字节
 * @remark 使用系统自带的解码器（Windows 下是 WIC），支持 PNG、JPEG、BMP
 */
bool LmLoadImageRGBA(const std::string& path, int maxSide, std::vector<uint8_t>& rgba, int& width, int& height);

#endif // LUMINA_IMAGE_LOADER_HPP
