# 字体 1:1 预览

这些图直接从运行时 UI 字模表 `ui_font12/14/16.h` 生成：

- 纯黑背景，只有白色文本；
- 1:1 像素，无缩放、无抗锯齿；
- 12px 按运行时规则加了 1px 字距；
- 尺寸：480×272 和 400×240。

| 字号 | 480×272 | 400×240 |
|---|---|---|
| 12px | `font12-480x272.png` | `font12-400x240.png` |
| 14px | `font14-480x272.png` | `font14-400x240.png` |
| 16px | `font16-480x272.png` | `font16-400x240.png` |

重新生成：

~~~powershell
python tools\make-font-preview.py
~~~
