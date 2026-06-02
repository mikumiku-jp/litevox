#pragma once

#include "model_asset.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CachedModelAsset {
    ModelAssetRecord modelAsset;
    std::string cacheKey;
    std::vector<uint8_t> modelBytes;
    uint64_t storedBytes = 0;
    uint64_t referenceCount = 0;
    uint64_t lastUsedOrder = 0;
};

struct ModelSessionCache {
    std::vector<CachedModelAsset> cachedModelAssets;
    std::map<std::string, size_t> cacheIndexByKey;
    uint64_t totalBytes = 0;
    uint64_t storedBytes = 0;
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
    uint64_t accessCount = 0;
    bool usedPartialRead = false;
};

void cacheModelAssets(ModelSessionCache &modelSessionCache, const std::vector<ModelAssetRecord> &modelAssets, bool shouldStoreBytes);
ModelSessionCache loadModelSessionCache(const std::vector<ModelAssetRecord> &modelAssets);
std::string createModelSessionCacheSummary(const ModelSessionCache &modelSessionCache);
