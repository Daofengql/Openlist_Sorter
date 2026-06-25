# OpenList Sorter

OpenList Sorter 是一个 Qt5 桌面端远程文件分类工具，用连续预览、tag 暂存和批量提交来加速 OpenList/Alist 目录整理。

启动时会先进入连接页。连接成功后才显示分类主窗口；主窗口顶部提供文件、编辑、帮助菜单。文件菜单里可以查看连接信息、打开设置或注销；注销会关闭自动登录、断开当前会话并关闭主窗口，不会清掉已保存的 endpoint/key。底部状态栏会显示当前 endpoint，并在连接后补充延迟、IP 和远端版本。

文件菜单的设置页可以配置默认本地下载目录、覆盖同名文件、保存时转 WebP 和分页预热并发数。分页预热并发数默认 2，范围限制为 1 到 10。主窗口保留当前源目录、分类保存和仅转换当前等常用动作；预览区域底部提供下载当前和删除当前图标按钮。删除当前会进行两次弹窗确认，成功后只更新本地分页列表，不重新拉取整个目录。下载和图片转换会优先复用本地 raw 原始字节缓存，避免重复下载。

图片可以启用“保存时转 WebP”：普通可直接读取的图片会使用原始远程文件字节转换，并按原始文件 SHA-256 命名后上传到一个或多个 tag 目标目录，全部成功后删除 OpenList 源目录中的原文件。HEIC/HEIF/AVIF 会先在内存里解码并写入一份满质量 JPG 缓存，再用这份 JPG 缓存转换 WebP，此时 SHA-256 来源也是这份 JPG 缓存。也可以使用“仅转换当前”，把当前图片转成 WebP 上传回原目录并删除原文件。

左侧目录文件列表默认每 20 个文件分页显示，文件编号仍然是整个目录内的全局编号。上一张/下一张跨页时会自动切换分页；待提交选择保存在内存里的完整文件列表上，不会因为翻页丢失。切换到某一页后，程序会按设置的并发数预热图片缓存：获取下载地址并下载原始数据；普通图片只写入 raw 缓存，HEIC/HEIF/AVIF 会额外生成满质量 JPG 解码缓存，底栏会显示读取进度。

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
  imagecodecs/
    # Windows
    CORE_RL_heif_.dll
    CORE_RL_webp_.dll
    CORE_RL_brotli_.dll
    CORE_RL_zlib_.dll
```

这些非 Qt 运行库不提交到 Git。Windows 换机器构建或打包时，把同样结构放到项目目录下即可；CMake 会复制到 `build-qt/dist/runtime/imagecodecs`。程序不再调用 ImageMagick/MagickWand 中间层。为了减少包体，`msvcp140*.dll` / `vcruntime140*.dll` 不再随包携带；干净系统如果无法加载 HEIF/WebP 运行库，安装 Microsoft Visual C++ 2015-2022 x64 Redistributable 即可。

构建完成后运行：

```powershell
.\build-qt\dist\OpenListSorter.exe
```

## 图片预览

图片解码按格式只走一条路径，避免同一格式同时打包多套库。Windows 上 JPG/PNG/BMP/GIF/TIFF/ICO 交给系统 WIC；WebP 解码和 WebP 输出交给 libwebp；HEIC/HEIF/AVIF 交给 libheif，并尽量关闭 libheif 默认的容器安全数量限制，避免大图或复杂 tile/grid 图片被策略拦住；SVG/SVGZ 继续使用 Qt SVG。

如果某种格式没有可用的底层解码器，预览会退回文件信息页，分类和保存功能不受影响。

图片相关缓存会写到程序运行目录下的 `cache/previews`。程序启动、进入分类模式前和退出时会自动清理这份缓存，避免目录持续膨胀。缓存 key 包含远程路径、大小和修改时间；raw 原始数据统一写为 `.bin`，HEIC/HEIF/AVIF 的解码缓存写为满质量 `.jpg`。Windows 上 JPG 缓存通过系统 WIC 在内存里编码，不依赖 Qt JPG 插件。

缩略图相关接口暂时保留，但当前版本不会主动请求 OpenList/Alist 缩略图；预览会优先复用本地缓存，缓存缺失时读取原图。

图片预览区域支持鼠标滚轮缩放、按住左键拖动查看，双击可复位到适配窗口。

## HTTPS / TLS

Qt 5.12 在 Windows 上需要 OpenSSL 运行库。项目会优先尝试从 `OPENLIST_SORTER_OPENSSL_ROOT` 和常见本机目录复制 `libssl` / `libcrypto` 到 `build-qt/dist`。

如果连接 HTTPS endpoint 时出现 `TLS initialization failed`，优先确认 `build-qt/dist` 里有与 Qt 兼容的 OpenSSL DLL，例如 `libssl-1_1-x64.dll` 和 `libcrypto-1_1-x64.dll`。
