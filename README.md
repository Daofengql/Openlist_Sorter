# OpenList Sorter

OpenList Sorter 是一个 Qt5 桌面端远程文件分类工具，用连续预览、tag 暂存和批量提交来加速 OpenList/Alist 目录整理。

启动时会先进入连接页。连接成功后才显示分类主窗口；主窗口顶部提供文件、编辑、帮助菜单，底部状态栏显示当前 endpoint。

主窗口可以设置默认本地下载目录，并下载当前远程文件。下载和图片转换会优先复用本地 raw 原始字节缓存，避免重复下载。

图片可以启用“保存时转 WebP”：保存分类时会先使用原始远程文件字节转换，并按原始文件 SHA-256 命名后上传到一个或多个 tag 目标目录，全部成功后删除 OpenList 源目录中的原文件。只有原始文件无法直接转换、必须通过 HEIC/libheif 等路径解码时，才会使用本地解码缓存转换，此时 SHA-256 来源也是这份解码缓存。也可以使用“仅转换当前”，把当前图片转成 WebP 上传回原目录并删除原文件。

左侧目录文件列表默认每 20 个文件分页显示，文件编号仍然是整个目录内的全局编号。上一张/下一张跨页时会自动切换分页；待提交选择保存在内存里的完整文件列表上，不会因为翻页丢失。切换到某一页后，程序会按页内顺序预热图片缓存：获取下载地址、下载原图、在本地解码并写入预览缓存，底栏会显示读取进度。

## Windows 构建

本项目结构和 Qt 路径传参方式参考 `D:\Projects\Draeyes\Draeyes-GL`。

```powershell
$env:Path = "D:\env\Qt\Qt5.12.9\Tools\mingw730_64\bin;D:\env\Qt\Qt5.12.9\5.12.9\mingw73_64\bin;$env:Path"

cmake -S . -B build-qt `
  -G "MinGW Makefiles" `
  -DOPENLIST_SORTER_QT_ROOT=D:/env/Qt/Qt5.12.9/5.12.9/mingw73_64 `
  -DOPENLIST_SORTER_OPENSSL_ROOT=D:/env/.espressif/tools/idf-git/2.39.2/mingw64/bin

cmake --build build-qt --target openlist_sorter -j 4
```

非 Qt 的图片运行库使用项目相对路径，默认从 `third_party/runtime` 读取：

```text
third_party/runtime/
  imagemagick/
    CORE_RL_MagickWand_.dll
    CORE_RL_heif_.dll
    CORE_RL_*.dll
    *.xml
    modules/
```

这些 DLL 不提交到 Git。换机器构建或打包时，把同样结构放到项目目录下即可；CMake 会复制到 `build-qt/dist/runtime`。

构建完成后运行：

```powershell
.\build-qt\dist\OpenListSorter.exe
```

## 图片预览

程序会先使用 Qt5 自带图片解码和 imageformats 插件预览图片。如果 Qt 无法直接读取 HEIC、AVIF、RAW 等格式，会尝试通过项目内的 ImageMagick MagickWand DLL 在内存中转码为 PNG 后显示。

如果 ImageMagick 也无法处理，例如遇到扩展名和真实容器不一致、或 HEIC/HEIF 元数据结构异常的图片，程序会绕过 MagickWand，直接复用 `CORE_RL_heif_.dll` 导出的 libheif API 从内存解码主图，并关闭 libheif 默认的容器安全数量限制，避免大图或复杂 tile/grid 图片被 ImageMagick 的策略拦住。

如果项目相对目录下没有 ImageMagick DLL，少见图片格式会退回文件信息页，分类和保存功能不受影响。

图片预览会缓存到程序运行目录下的 `cache/previews`。程序启动、进入分类模式前和退出时会自动清理这份缓存，避免目录持续膨胀。缓存 key 包含远程路径、大小和修改时间；预览缓存使用满质量 JPG，避免复杂 HEIC/HEIF 解码后生成体积巨大的 PNG。

如果 OpenList/Alist 返回了图片缩略图，程序会先尝试加载并缓存缩略图；缩略图尺寸足够时直接用于预览，尺寸不足时再下载原图解码。

图片预览区域支持鼠标滚轮缩放、按住左键拖动查看，双击可复位到适配窗口。

## HTTPS / TLS

Qt 5.12 在 Windows 上需要 OpenSSL 运行库。项目会优先尝试从 `OPENLIST_SORTER_OPENSSL_ROOT` 和常见本机目录复制 `libssl` / `libcrypto` 到 `build-qt/dist`。

如果连接 HTTPS endpoint 时出现 `TLS initialization failed`，优先确认 `build-qt/dist` 里有与 Qt 兼容的 OpenSSL DLL，例如 `libssl-1_1-x64.dll` 和 `libcrypto-1_1-x64.dll`。
