#include "model_session_cache.hpp"

#include "vvm_archive.hpp"

#include <sstream>
#include <utility>

static std::string createModelSessionCacheKey(const ModelAssetRecord &modelAsset) {
    return modelAsset.archivePath.string() + ":" + modelAsset.entryName;
}

void cacheModelAssets(ModelSessionCache &modelSessionCache, const std::vector<ModelAssetRecord> &modelAssets, bool shouldStoreBytes) {
    modelSessionCache.usedPartialRead = true;
    modelSessionCache.cachedModelAssets.reserve(modelSessionCache.cachedModelAssets.size() + modelAssets.size());
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        modelSessionCache.accessCount++;
        std::string cacheKey = createModelSessionCacheKey(modelAsset);
        auto cachedIndexIterator = modelSessionCache.cacheIndexByKey.find(cacheKey);
        if (cachedIndexIterator != modelSessionCache.cacheIndexByKey.end()) {
            CachedModelAsset &cachedModelAsset = modelSessionCache.cachedModelAssets[cachedIndexIterator->second];
            cachedModelAsset.referenceCount++;
            cachedModelAsset.lastUsedOrder = modelSessionCache.accessCount;
            modelSessionCache.cacheHits++;
            continue;
        }
        CachedModelAsset cachedModelAsset;
        cachedModelAsset.modelAsset = modelAsset;
        cachedModelAsset.cacheKey = cacheKey;
        cachedModelAsset.referenceCount = 1;
        cachedModelAsset.lastUsedOrder = modelSessionCache.accessCount;
        modelSessionCache.totalBytes += modelAsset.uncompressedSize;
        if (shouldStoreBytes) {
            cachedModelAsset.modelBytes = extractVvmEntryBytesAt(modelAsset.archivePath, modelAsset.entryName, modelAsset.dataOffset, modelAsset.compressedSize, modelAsset.uncompressedSize, modelAsset.compressionMethod);
            cachedModelAsset.storedBytes = cachedModelAsset.modelBytes.size();
            modelSessionCache.storedBytes += cachedModelAsset.storedBytes;
        }
        modelSessionCache.cacheIndexByKey[cacheKey] = modelSessionCache.cachedModelAssets.size();
        modelSessionCache.cacheMisses++;
        modelSessionCache.cachedModelAssets.push_back(std::move(cachedModelAsset));
    }
}

ModelSessionCache loadModelSessionCache(const std::vector<ModelAssetRecord> &modelAssets) {
    ModelSessionCache modelSessionCache;
    cacheModelAssets(modelSessionCache, modelAssets, true);
    return modelSessionCache;
}

std::string createModelSessionCacheSummary(const ModelSessionCache &modelSessionCache) {
    std::ostringstream summaryStream;
    summaryStream << "asset_count\t" << modelSessionCache.cachedModelAssets.size() << "\n";
    summaryStream << "total_bytes\t" << modelSessionCache.totalBytes << "\n";
    summaryStream << "stored_bytes\t" << modelSessionCache.storedBytes << "\n";
    summaryStream << "cache_hits\t" << modelSessionCache.cacheHits << "\n";
    summaryStream << "cache_misses\t" << modelSessionCache.cacheMisses << "\n";
    summaryStream << "cache_accesses\t" << modelSessionCache.accessCount << "\n";
    summaryStream << "partial_read\t" << (modelSessionCache.usedPartialRead ? "true" : "false") << "\n";
    summaryStream << "cache_key\treference_count\tlast_used_order\tstored_bytes\tvvm\tasset\n";
    for (const CachedModelAsset &cachedModelAsset : modelSessionCache.cachedModelAssets) {
        summaryStream << cachedModelAsset.cacheKey << "\t"
                      << cachedModelAsset.referenceCount << "\t"
                      << cachedModelAsset.lastUsedOrder << "\t"
                      << cachedModelAsset.storedBytes << "\t"
                      << cachedModelAsset.modelAsset.archivePath.filename().string() << "\t"
                      << cachedModelAsset.modelAsset.entryName << "\n";
    }
    return summaryStream.str();
}
