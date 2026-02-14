#include "../GlobalBase/GlobalBase/GB_Utf8String.h"
#include "../GlobalBase/GlobalBase/GB_FileSystem.h"
#include "../GlobalBase/GlobalBase/GB_Crypto.h"
#include "../GlobalBase/GlobalBase/GB_IO.h"
#include "../GlobalBase/GlobalBase/GB_Logger.h"
#include "../GlobalBase/GlobalBase/GB_Timer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <omp.h>

struct FileInfo
{
    std::string filePathUtf8 = "";
    size_t fileSizeBytes = 0;
    std::string md5 = "";
};

static std::string MakeSafeFileName(const std::string& raw)
{
    std::string result = raw;
    for (size_t i = 0; i < result.size(); i++)
    {
        const char c = result[i];
        if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            result[i] = '-';
        }
    }
    return result;
}

static std::string FormatBytes(size_t bytes)
{
    std::ostringstream oss;
    oss << bytes << " B";
    if (bytes >= 1024ULL)
    {
        oss << " (" << std::fixed << std::setprecision(3) << (static_cast<double>(bytes) / 1024.0) << " KB)";
    }
    if (bytes >= 1024ULL * 1024ULL)
    {
        oss << " (" << std::fixed << std::setprecision(3) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB)";
    }
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
    {
        oss << " (" << std::fixed << std::setprecision(3) << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB)";
    }
    return oss.str();
}

// 仅保留“文件大小出现次数 > 1”的候选（因为重复文件一定在这里面）
static void KeepOnlyNonUniqueFileSizes(std::vector<FileInfo>& allFiles)
{
    if (allFiles.empty())
    {
        return;
    }

    std::unordered_map<size_t, int> sizeCounts;
    sizeCounts.reserve(allFiles.size());
    for (const auto& file : allFiles)
    {
        sizeCounts[file.fileSizeBytes]++;
    }

    const auto newEnd = std::remove_if(allFiles.begin(), allFiles.end(), [&sizeCounts](const FileInfo& file) {
        const auto it = sizeCounts.find(file.fileSizeBytes);
        if (it == sizeCounts.end())
        {
            return true;
        }
        return it->second <= 1;
    });

    allFiles.erase(newEnd, allFiles.end());
}

static std::string BuildDuplicateReport(const std::vector<std::string>& sourceDirsUtf8, size_t totalFiles, size_t candidateFiles, const std::vector<std::vector<FileInfo>>& duplicateGroups, size_t duplicatedFileCount, size_t wastedBytes)
{
    std::ostringstream oss;
    oss << "Duplicate File Scan Report\n";
    oss << "Time: " << GetLocalTimeStr(true, true) << "\n\n";

    oss << "Source Directories:\n";
    for (const auto& dir : sourceDirsUtf8)
    {
        oss << "  - " << dir << "\n";
    }
    oss << "\n";

    oss << "Summary:\n";
    oss << "  Total files scanned: " << totalFiles << "\n";
    oss << "  Candidate files (same size): " << candidateFiles << "\n";
    oss << "  Duplicate groups (same size + same md5): " << duplicateGroups.size() << "\n";
    oss << "  Duplicated files (excluding one kept per group): " << duplicatedFileCount << "\n";
    oss << "  Potential space saving (approx): " << FormatBytes(wastedBytes) << "\n\n";

    if (duplicateGroups.empty())
    {
        oss << "No duplicate files found.\n";
        return oss.str();
    }

    oss << "Duplicate Groups Detail:\n";
    for (size_t groupIndex = 0; groupIndex < duplicateGroups.size(); groupIndex++)
    {
        const auto& group = duplicateGroups[groupIndex];
        if (group.empty())
        {
            continue;
        }

        oss << "\n[Group " << (groupIndex + 1) << "]\n";
        oss << "  File size: " << FormatBytes(group[0].fileSizeBytes) << "\n";
        oss << "  MD5: " << group[0].md5 << "\n";
        oss << "  Count: " << group.size() << "\n";
        oss << "  Files:\n";
        for (const auto& file : group)
        {
            oss << "    - " << file.filePathUtf8 << "\n";
        }
    }

    oss << "\n";
    return oss.str();
}

int main(int argc, char* argv[])
{
    GB_SetConsoleEncodingToUtf8();
    

    std::vector<std::string> sourceDirsUtf8;
    sourceDirsUtf8.reserve(static_cast<size_t>(std::max(argc - 1, 0)));
    for (int i = 1; i < argc; i++)
    {
        const std::string argUtf8 = GB_AnsiToUtf8(argv[i]);
        sourceDirsUtf8.push_back(argUtf8);
    }

    std::vector<std::string> filesPathUtf8;
    for (const std::string& sourceDirUtf8 : sourceDirsUtf8)
    {
        if (!GB_IsDirectoryExists(sourceDirUtf8))
        {
            GBLOG_WARNING(GB_STR("目录不存在: ") + sourceDirUtf8);
            continue;
        }

        std::vector<std::string> filesList = GB_GetFilesList(sourceDirUtf8, true);
        filesPathUtf8.insert(filesPathUtf8.end(), filesList.begin(), filesList.end());
    }

    // 避免扫描目录重叠时产生重复路径
    std::sort(filesPathUtf8.begin(), filesPathUtf8.end());
    filesPathUtf8.erase(std::unique(filesPathUtf8.begin(), filesPathUtf8.end()), filesPathUtf8.end());

    const size_t numFiles = filesPathUtf8.size();
    std::vector<FileInfo> allFiles(numFiles);

#pragma omp parallel for
    for (long long i = 0; i < static_cast<long long>(numFiles); i++)
    {
        allFiles[static_cast<size_t>(i)].filePathUtf8 = filesPathUtf8[static_cast<size_t>(i)];
        allFiles[static_cast<size_t>(i)].fileSizeBytes = GB_GetFileSizeByte(allFiles[static_cast<size_t>(i)].filePathUtf8);
    }

    // 只保留 size 重复的候选
    std::vector<FileInfo> candidates = allFiles;
    KeepOnlyNonUniqueFileSizes(candidates);

    // 计算 md5
#pragma omp parallel for
    for (long long i = 0; i < static_cast<long long>(candidates.size()); i++)
    {
        FileInfo& file = candidates[static_cast<size_t>(i)];

        const GB_ByteBuffer buffer = GB_ReadFileToBinary(file.filePathUtf8);

        // 读失败：buffer 为空但文件大小不为 0（0 字节文件是合法情况）
        if (buffer.empty() && file.fileSizeBytes != 0)
        {
            file.md5 = "";
            continue;
        }
        file.md5 = GB_Md5Hash(GB_ByteBufferToString(buffer));
    }

    // 按 (size + md5) 分组
    std::unordered_map<std::string, std::vector<FileInfo>> groups;
    groups.reserve(candidates.size());

    for (const auto& file : candidates)
    {
        if (file.md5.empty())
        {
            continue;
        }
        const std::string key = std::to_string(file.fileSizeBytes) + "_" + file.md5;
        groups[key].push_back(file);
    }

    std::vector<std::vector<FileInfo>> duplicateGroups;
    duplicateGroups.reserve(groups.size());

    size_t duplicatedFileCount = 0;
    size_t wastedBytes = 0;

    for (const auto& kv : groups)
    {
        const std::vector<FileInfo>& group = kv.second;
        if (group.size() >= 2)
        {
            duplicateGroups.push_back(group);

            duplicatedFileCount += (group.size() - 1);
            wastedBytes += group[0].fileSizeBytes * (group.size() - 1);
        }
    }

    const std::string reportText = BuildDuplicateReport(sourceDirsUtf8, numFiles, candidates.size(), duplicateGroups, duplicatedFileCount, wastedBytes);

    // 输出到桌面（优先），失败则输出到 exe 目录
    std::string outDir = GB_GetDesktopDirectory();
    if (outDir.empty())
    {
        outDir = GB_GetExeDirectory();
    }

    const std::string timeStr = MakeSafeFileName(GetLocalTimeStr(true, true));
    const std::string outPathUtf8 = GB_JoinPath(outDir, "DuplicateScan_" + timeStr + ".txt");

    const bool ok = GB_WriteUtf8ToFile(outPathUtf8, reportText, false, true);
    if (ok)
    {
        std::cout << GB_STR("扫描结果已输出到: ") << outPathUtf8 << std::endl;
    }
    else
    {
        std::cout << GB_STR("写入扫描结果失败: ") << outPathUtf8 << std::endl;
    }

    return 0;
}
