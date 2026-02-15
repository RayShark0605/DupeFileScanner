# DupeFileScanner

**DupeFileScanner** 是一个命令行重复文件扫描器：递归扫描你指定的一个或多个目录，先用 **文件大小** 筛出候选，再对候选计算 **MD5**，最终按 **(size + md5)** 分组输出重复文件报告（`.txt`）。

> 判重规则：文件大小一致 + MD5 一致（认为文件内容一致）。

## 工作流程

1. 递归收集所有文件路径（支持传入多个目录）。
2. 去重文件路径（避免目录重叠导致同一文件被扫描两次）。
3. 并行统计每个文件大小。
4. 仅保留“大小出现次数 > 1”的候选文件（重复文件一定在这里面）。
5. 对候选文件并行读取二进制并计算 MD5。
6. 按 (size + md5) 分组，筛出每组数量 >= 2 的重复组。
7. 生成文本报告并写入本地。

## 构建（Windows / Visual Studio）

该仓库包含 Visual Studio 解决方案 `DupeFileScanner.sln`，并使用子模块依赖 `GlobalBase`。

### 1) 克隆（包含子模块）

```bash
git clone --recurse-submodules https://github.com/RayShark0605/DupeFileScanner.git
```

如果你已经克隆过但忘记拉子模块：

```bash
git submodule update --init --recursive
```

### 2) 编译

- 使用 **Visual Studio 2022** 打开 `DupeFileScanner.sln`
- 选择 `Release | x64`
- Build

## 使用方法

运行可执行文件并传入要扫描的目录列表：

```bash
DupeFileScanner.exe "D:\Data" "E:\Backup" "C:\Users\YourName\Desktop"
```

- 可以传入多个目录
- 不存在的目录会被跳过（日志会提示）
- 若目录存在重叠（父/子目录同时传入），程序会对收集到的文件路径做去重，避免重复扫描

## 输出报告

程序会生成一份报告文件，文件名类似：

- `DuplicateScan_YYYY-MM-DD HH-MM-SS.txt`（实际以本地时间字符串为准）

输出目录规则：

1. 优先输出到 **桌面目录**
2. 若无法获取桌面目录，则输出到 **exe 所在目录**

报告内容包括：

- 扫描时间与扫描目录清单
- 总文件数、候选文件数（同大小）、重复组数量
- 重复文件数量（每组默认“保留 1 个”，其余计为重复）
- 潜在节省空间估算
- 每个重复组的详细列表（大小、MD5、文件路径）

## 注意事项（性能 / 内存）

- 目前对候选文件计算 MD5 时，会将文件 **整体读入内存** 再进行哈希。如果候选文件中存在大量大文件，可能出现较高峰值内存占用。
  - 可优化方向：改为“流式读取 + 增量 MD5”（降低峰值内存）。
- MD5 仅用于“快速判断文件内容一致”，不用于密码学安全场景。

## 许可证

本项目采用 **MIT License**，详见 [LICENSE](LICENSE)。
