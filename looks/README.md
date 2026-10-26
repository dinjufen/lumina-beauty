# 素材文件格式（lumina-look）

一个素材是一个 JSON 文件，描述一款风格：**调色**（`color`）和**妆容**（`makeup`），两部分都可以只写一个。
加载后它会代替内置的风格妆，强度仍由「风格妆色彩」和「风格妆妆容」两个滑杆控制。

这个目录里的 26 个文件就是内置的 15 款滤镜（`filter-*.json`，只有调色）和 11 款风格妆（`style-*.json`）。
它们由 `lumina-look export` 导出，读回后与内置效果逐像素一致，可以直接复制一份来改。

## 怎么用

- **OBS**：滤镜属性「调色」组里的「素材文件」，选一个 `.json`。清空后回到下拉框里选的内置风格。
- **SDK**：`lumina_set_look_file(engine, "path/to/look.json")`，或者用 `lumina_set_look_json` 直接传 JSON 文本。
  传 `NULL` 或空字符串恢复内置风格。读取失败返回 0，日志里写明原因，之前的效果保持不变。
- **命令行**：`lumina-cli <models> 1280 720 in.i420 out.i420 look=my-look.json style_color=1 style_makeup=1`
- **检查文件**：`lumina-look check my-look.json` 会指出哪一行、哪个字段有问题。

## 示例

```json
{
  "format": "lumina-look",
  "version": 1,
  "name": "樱花",
  "color": {
    "temperature": -0.02,
    "tint": 0.06,
    "brightness": 0.15,
    "skin": 0.05
  },
  "makeup": {
    "lips": {"color": "#F07C8C", "opacity": 0.6},
    "blush": {"color": [1.0, 0.78, 0.82], "opacity": 0.5, "size": 0.18},
    "eyeliner": {"opacity": 0.4},
    "lashes": {"opacity": 0.6},
    "foundation": 0.2
  }
}
```

没写的字段都取中性值（不改变画面），所以只写想改的部分就行。

## 字段

顶层：

| 字段 | 说明 |
|---|---|
| `format` | 必须是 `"lumina-look"` |
| `version` | 格式版本，目前是 `1`，可省略 |
| `name` | 名字，显示在日志里 |
| `color` | 调色，可省略 |
| `makeup` | 妆容，可省略；`color` 和 `makeup` 至少要有一个 |

`color`（调色）：

| 字段 | 范围 | 说明 |
|---|---|---|
| `temperature` | -1 到 1 | 色温，正值偏暖 |
| `tint` | -1 到 1 | 色调，正值偏洋红，负值偏绿 |
| `saturation` | -1 到 1 | 饱和度 |
| `contrast` | -1 到 1 | 对比度 |
| `brightness` | -1 到 1 | 亮度 |
| `fade` | -1 到 1 | 褪色：正值抬高暗部（胶片感），负值压暗暗部 |
| `shadow_tint` | 每个通道 -0.5 到 0.5 | 暗部染色 `[r, g, b]`，内置效果一般在 ±0.05 以内 |
| `highlight_tint` | 每个通道 -0.5 到 0.5 | 亮部染色 `[r, g, b]` |
| `skin` | 0 到 1 | 只提亮皮肤，让脸比环境更亮 |

内置效果的数值都比较小（多在 ±0.3 以内）；滑杆拉满时调色会再乘一个整体增益，数值太大容易过曝或偏色。

`makeup`（妆容）里的每个部位都是 `{"color": ..., "opacity": ...}`：

| 部位 | 说明 |
|---|---|
| `lips` | 口红 |
| `blush` | 腮红，多一个 `size`（半径，相对脸宽，0.05 到 0.4，默认 0.17） |
| `brows` | 眉毛 |
| `eye_shadow` | 眼影 |
| `contour` | 修容（颧骨下方的阴影） |
| `eyeliner` | 眼线，颜色默认深棕 `[0.22, 0.18, 0.18]` |
| `lashes` | 睫毛，颜色默认近黑 `[0.12, 0.10, 0.10]` |
| `foundation` | 粉底提亮量，0 到 1，直接是数字 |

- **颜色**可以写成 `[r, g, b]`（每个通道 0 到 1）或 `"#RRGGBB"`。妆容用「正片叠底」叠到皮肤上：
  白色 `[1, 1, 1]` 不改变皮肤，颜色越深压得越暗。所以口红要选比想要的效果更浅、更饱和的颜色。
- **opacity** 是滑杆拉满时的浓度，0 到 1。省略的部位浓度为 0，也就是不画。
- 超出范围的数值会被截到范围内。

## 以后

这是第 1 版格式，只有参数。之后计划支持贴图妆容（在标准脸模板上画的 PNG，按关键点贴到脸上），
会在 `version` 升级时向下兼容，已有的素材文件不用改。
