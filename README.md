# OpenList Sorter

OpenList Sorter 是一个 Qt5 桌面端远程文件分类工具，用连续预览、tag 暂存和批量提交来加速 OpenList/Alist 目录整理。

详细设计见 [DESIGN.md](DESIGN.md)。

启动时会先进入连接页。连接成功后才显示分类主窗口；主窗口顶部提供文件、编辑、帮助菜单，底部状态栏显示当前 endpoint。

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

构建完成后运行：

```powershell
.\build-qt\dist\OpenListSorter.exe
```

## 图片预览

程序会先使用 Qt5 自带图片解码和 imageformats 插件预览图片。如果 Qt 无法直接读取 HEIC、AVIF、RAW 等格式，会尝试调用本机 `magick.exe` 临时转码为 PNG 后显示。

本机已经安装 ImageMagick 时通常不需要额外配置；如果没有安装，少见图片格式会退回文件信息页，分类和保存功能不受影响。

## HTTPS / TLS

Qt 5.12 在 Windows 上需要 OpenSSL 运行库。项目会优先尝试从 `OPENLIST_SORTER_OPENSSL_ROOT` 和常见本机目录复制 `libssl` / `libcrypto` 到 `build-qt/dist`。

如果连接 HTTPS endpoint 时出现 `TLS initialization failed`，优先确认 `build-qt/dist` 里有与 Qt 兼容的 OpenSSL DLL，例如 `libssl-1_1-x64.dll` 和 `libcrypto-1_1-x64.dll`。
