#pragma once

#include "vvm_archive.hpp"

#include <cstddef>
#include <string>
#include <vector>

std::string createVvBinInspectText(const std::vector<VvmArchiveSummary> &archiveSummaries, size_t sampleBytes, bool shouldScanWholeAsset);
