/*
 * Copyright (C) 2021-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "OriginStorageManager.h"

#include "BackgroundFetchStoreManager.h"
#include "CacheStorageManager.h"
#include "CacheStorageRegistry.h"
#include "FileSystemStorageHandleRegistry.h"
#include "FileSystemStorageManager.h"
#include "IDBStorageManager.h"
#include "IDBStorageRegistry.h"
#include "LocalStorageManager.h"
#include "Logging.h"
#include "MemoryStorageArea.h"
#include "ServiceWorkerStorageManager.h"
#include "SessionStorageManager.h"
#include "StorageAreaRegistry.h"
#include "UnifiedOriginStorageLevel.h"
#include "WebsiteDataType.h"
#include <WebCore/SQLiteFileSystem.h>
#include <WebCore/StorageEstimate.h>
#include <algorithm>
#include <wtf/FileSystem.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

static constexpr auto originFileName = "origin"_s;
enum class OriginStorageManager::StorageBucketMode : bool { BestEffort, Persistent };

WTF_MAKE_TZONE_ALLOCATED_IMPL(OriginStorageManager);

class OriginStorageManager::StorageBucket {
    WTF_MAKE_TZONE_ALLOCATED(OriginStorageManager::StorageBucket);
public:
    enum class StorageType : uint8_t {
        FileSystem,
        LocalStorage,
        SessionStorage,
        IndexedDB,
        CacheStorage,
        BackgroundFetchStorage,
        ServiceWorkerStorage,
    };
    std::optional<StorageType> toStorageType(WebsiteDataType) const;
    String toStorageIdentifier(StorageType) const;
    StorageBucket(const String& rootPath, const String& identifier, const String& localStoragePath, const String& idbStoragePath, const String& cacheStoragePath, UnifiedOriginStorageLevel);
    StorageBucketMode mode() const { return m_mode; }
    void setMode(StorageBucketMode mode) { m_mode = mode; }
    void connectionClosed(IPC::Connection::UniqueID);
    String typeStoragePath(StorageType) const;
    FileSystemStorageManager& fileSystemStorageManager(FileSystemStorageHandleRegistry&, FileSystemStorageManager::QuotaCheckFunction&&);
    FileSystemStorageManager* existingFileSystemStorageManager() { return m_fileSystemStorageManager.get(); }
    LocalStorageManager& localStorageManager(StorageAreaRegistry&);
    LocalStorageManager* existingLocalStorageManager() { return m_localStorageManager.get(); }
    SessionStorageManager& sessionStorageManager(StorageAreaRegistry&);
    SessionStorageManager* existingSessionStorageManager() { return m_sessionStorageManager.get(); }
    IDBStorageManager& idbStorageManager(IDBStorageRegistry&, IDBStorageManager::QuotaCheckFunction&&);
    IDBStorageManager* existingIDBStorageManager() { return m_idbStorageManager.get(); }
    CacheStorageManager& cacheStorageManager(CacheStorageRegistry&, const WebCore::ClientOrigin&, CacheStorageManager::QuotaCheckFunction&&, Ref<WorkQueue>&&);
    CacheStorageManager* existingCacheStorageManager() { return m_cacheStorageManager.get(); }
    BackgroundFetchStoreManager& backgroundFetchManager(Ref<WorkQueue>&&, BackgroundFetchStoreManager::QuotaCheckFunction&&);
    ServiceWorkerStorageManager& serviceWorkerStorageManager();
    uint64_t cacheStorageSize();
    void closeCacheStorageManager();
    bool isActive() const;
    bool hasDataInMemory() const;
    bool isEmpty();
    DataTypeSizeMap fetchDataTypesInList(OptionSet<WebsiteDataType>, bool shouldComputeSize);
    void deleteData(OptionSet<WebsiteDataType>, WallTime);
    void moveData(OptionSet<WebsiteDataType>, const String& localStoragePath, const String& idbStoragePath);
    void deleteEmptyDirectory();
    String resolvedLocalStoragePath();
    String resolvedIDBStoragePath();
    String resolvedCacheStoragePath();
    String resolvedBackgroundFetchStoragePath();
    String resolvedPath(WebsiteDataType);

private:
    OptionSet<WebsiteDataType> fetchDataTypesInListFromMemory(OptionSet<WebsiteDataType>);
    DataTypeSizeMap fetchDataTypesInListFromDisk(OptionSet<WebsiteDataType>, bool shouldComputeSize);
    void deleteFileSystemStorageData(WallTime);
    void deleteLocalStorageData(WallTime);
    void deleteSessionStorageData();
    void deleteIDBStorageData(WallTime);
    void deleteCacheStorageData(WallTime);

    String m_rootPath;
    String m_identifier;
    StorageBucketMode m_mode { StorageBucketMode::BestEffort };
    RefPtr<FileSystemStorageManager> m_fileSystemStorageManager;
    std::unique_ptr<LocalStorageManager> m_localStorageManager;
    String m_customLocalStoragePath;
    String m_resolvedLocalStoragePath;
    std::unique_ptr<SessionStorageManager> m_sessionStorageManager;
    std::unique_ptr<IDBStorageManager> m_idbStorageManager;
    String m_customIDBStoragePath;
    String m_resolvedIDBStoragePath;
    RefPtr<CacheStorageManager> m_cacheStorageManager;
    String m_customCacheStoragePath;
    String m_resolvedCacheStoragePath;
    UnifiedOriginStorageLevel m_level;
    RefPtr<BackgroundFetchStoreManager> m_backgroundFetchManager;
    std::unique_ptr<ServiceWorkerStorageManager> m_serviceWorkerStorageManager;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(OriginStorageManager::StorageBucket);

OriginStorageManager::StorageBucket::StorageBucket(const String& rootPath, const String& identifier, const String& localStoragePath, const String& idbStoragePath, const String& cacheStoragePath, UnifiedOriginStorageLevel level)
    : m_rootPath(rootPath)
    , m_identifier(identifier)
    , m_customLocalStoragePath(localStoragePath)
    , m_customIDBStoragePath(idbStoragePath)
    , m_customCacheStoragePath(cacheStoragePath)
    , m_level(level)
{
}

void OriginStorageManager::StorageBucket::connectionClosed(IPC::Connection::UniqueID connection)
{
    if (RefPtr fileSystemStorageManager = m_fileSystemStorageManager)
        fileSystemStorageManager->connectionClosed(connection);

    if (m_localStorageManager)
        m_localStorageManager->connectionClosed(connection);

    if (m_sessionStorageManager)
        m_sessionStorageManager->connectionClosed(connection);

    if (RefPtr manager = m_cacheStorageManager)
        manager->connectionClosed(connection);
}

std::optional<OriginStorageManager::StorageBucket::StorageType> OriginStorageManager::StorageBucket::toStorageType(WebsiteDataType websiteDataType) const
{
    switch (websiteDataType) {
    case WebsiteDataType::FileSystem:
        return StorageType::FileSystem;
    case WebsiteDataType::LocalStorage:
        return StorageType::LocalStorage;
    case WebsiteDataType::SessionStorage:
        return StorageType::SessionStorage;
    case WebsiteDataType::IndexedDBDatabases:
        return StorageType::IndexedDB;
    case WebsiteDataType::DOMCache:
        return StorageType::CacheStorage;
    case WebsiteDataType::BackgroundFetchStorage:
        return StorageType::BackgroundFetchStorage;
    case WebsiteDataType::ServiceWorkerRegistrations:
        return StorageType::ServiceWorkerStorage;
    default:
        break;
    }

    ASSERT_NOT_REACHED();
    return std::nullopt;
}

String OriginStorageManager::StorageBucket::toStorageIdentifier(StorageType type) const
{
    switch (type) {
    case StorageType::FileSystem:
        return "FileSystem"_s;
    case StorageType::LocalStorage:
        return "LocalStorage"_s;
    case StorageType::SessionStorage:
        return "SessionStorage"_s;
    case StorageType::IndexedDB:
        return "IndexedDB"_s;
    case StorageType::CacheStorage:
        return "CacheStorage"_s;
    case StorageType::BackgroundFetchStorage:
        return "BackgroundFetchStorage"_s;
    case StorageType::ServiceWorkerStorage:
        return "ServiceWorkers"_s;
    default:
        break;
    }
    ASSERT_NOT_REACHED();
    return emptyString();
}

String OriginStorageManager::StorageBucket::typeStoragePath(StorageType type) const
{
    auto storageIdentifier = toStorageIdentifier(type);
    if (m_rootPath.isEmpty() || storageIdentifier.isEmpty())
        return emptyString();

    return FileSystem::pathByAppendingComponent(m_rootPath, storageIdentifier);
}

FileSystemStorageManager& OriginStorageManager::StorageBucket::fileSystemStorageManager(FileSystemStorageHandleRegistry& registry, FileSystemStorageManager::QuotaCheckFunction&& quotaCheckFunction)
{
    if (!m_fileSystemStorageManager)
        m_fileSystemStorageManager = FileSystemStorageManager::create(typeStoragePath(StorageType::FileSystem), registry, WTFMove(quotaCheckFunction));

    return *m_fileSystemStorageManager;
}

LocalStorageManager& OriginStorageManager::StorageBucket::localStorageManager(StorageAreaRegistry& registry)
{
    if (!m_localStorageManager)
        m_localStorageManager = makeUnique<LocalStorageManager>(resolvedLocalStoragePath(), registry);

    return *m_localStorageManager;
}

SessionStorageManager& OriginStorageManager::StorageBucket::sessionStorageManager(StorageAreaRegistry& registry)
{
    if (!m_sessionStorageManager)
        m_sessionStorageManager = makeUnique<SessionStorageManager>(registry);

    return *m_sessionStorageManager;
}

IDBStorageManager& OriginStorageManager::StorageBucket::idbStorageManager(IDBStorageRegistry& registry, IDBStorageManager::QuotaCheckFunction&& quotaCheckFunction)
{
    if (!m_idbStorageManager)
        m_idbStorageManager = makeUnique<IDBStorageManager>(resolvedIDBStoragePath(), registry, WTFMove(quotaCheckFunction));

    return *m_idbStorageManager;
}

CacheStorageManager& OriginStorageManager::StorageBucket::cacheStorageManager(CacheStorageRegistry& registry, const WebCore::ClientOrigin& origin, CacheStorageManager::QuotaCheckFunction&& quotaCheckFunction, Ref<WorkQueue>&& queue)
{
    if (!m_cacheStorageManager) {
        std::optional<WebCore::ClientOrigin> optionalOrigin;
        if (m_level < UnifiedOriginStorageLevel::Standard)
            optionalOrigin = origin;
        m_cacheStorageManager = CacheStorageManager::create(resolvedCacheStoragePath(), registry, optionalOrigin, WTFMove(quotaCheckFunction), WTFMove(queue));
    }

    return *m_cacheStorageManager;
}

BackgroundFetchStoreManager& OriginStorageManager::StorageBucket::backgroundFetchManager(Ref<WorkQueue>&& queue, BackgroundFetchStoreManager::QuotaCheckFunction&& quotaCheckFunction)
{
    if (!m_backgroundFetchManager)
        m_backgroundFetchManager = BackgroundFetchStoreManager::create(resolvedBackgroundFetchStoragePath(), WTFMove(queue), WTFMove(quotaCheckFunction));

    return *m_backgroundFetchManager;
}

ServiceWorkerStorageManager& OriginStorageManager::StorageBucket::serviceWorkerStorageManager()
{
    RELEASE_ASSERT(m_level >= UnifiedOriginStorageLevel::Standard);

    if (!m_serviceWorkerStorageManager)
        m_serviceWorkerStorageManager = makeUnique<ServiceWorkerStorageManager>(resolvedPath(WebsiteDataType::ServiceWorkerRegistrations));

    return *m_serviceWorkerStorageManager;
}

bool OriginStorageManager::StorageBucket::isActive() const
{
    // We cannot remove the bucket if it has in-memory data, otherwise session
    // data may be lost.
    RefPtr fileSystemStorageManager = m_fileSystemStorageManager;
    return (fileSystemStorageManager && fileSystemStorageManager->isActive())
        || (m_localStorageManager && m_localStorageManager->isActive())
        || (m_sessionStorageManager && m_sessionStorageManager->isActive())
        || (m_idbStorageManager && m_idbStorageManager->isActive())
        || (m_cacheStorageManager && RefPtr { m_cacheStorageManager }->isActive());
}

bool OriginStorageManager::StorageBucket::hasDataInMemory() const
{
    return (m_localStorageManager && m_localStorageManager->hasDataInMemory())
        || (m_sessionStorageManager && m_sessionStorageManager->hasDataInMemory())
        || (m_idbStorageManager && m_idbStorageManager->hasDataInMemory())
        || (m_cacheStorageManager && RefPtr { m_cacheStorageManager }->hasDataInMemory());
}

bool OriginStorageManager::StorageBucket::isEmpty()
{
    ASSERT(!RunLoop::isMain());

    auto files = FileSystem::listDirectory(m_rootPath);
    auto hasValidFile = std::ranges::any_of(files, [&](auto file) {
        bool isInvalidFile = (file == originFileName);
#if PLATFORM(COCOA)
        isInvalidFile |= (file == ".DS_Store"_s);
#endif
        return !isInvalidFile;
    });
    if (hasValidFile)
        return false;

    auto idbStorageFiles = FileSystem::listDirectory(resolvedIDBStoragePath());
    return !FileSystem::fileExists(resolvedLocalStoragePath()) && idbStorageFiles.isEmpty() && !CacheStorageManager::hasCacheList(resolvedCacheStoragePath());
}

OriginStorageManager::DataTypeSizeMap OriginStorageManager::StorageBucket::fetchDataTypesInList(OptionSet<WebsiteDataType> types, bool shouldComputeSize)
{
    auto result = fetchDataTypesInListFromDisk(types, shouldComputeSize);
    auto memoryResult = fetchDataTypesInListFromMemory(types);
    for (auto type : memoryResult)
        result.add(type, 0);

    return result;
}

OptionSet<WebsiteDataType> OriginStorageManager::StorageBucket::fetchDataTypesInListFromMemory(OptionSet<WebsiteDataType> types)
{
    OptionSet<WebsiteDataType> result;
    if (types.contains(WebsiteDataType::LocalStorage)) {
        if (m_localStorageManager && m_localStorageManager->hasDataInMemory())
            result.add(WebsiteDataType::LocalStorage);
    }

    if (types.contains(WebsiteDataType::SessionStorage)) {
        if (m_sessionStorageManager && m_sessionStorageManager->hasDataInMemory())
            result.add(WebsiteDataType::SessionStorage);
    }

    if (types.contains(WebsiteDataType::IndexedDBDatabases)) {
        if (m_idbStorageManager && m_idbStorageManager->hasDataInMemory())
            result.add(WebsiteDataType::IndexedDBDatabases);
    }

    if (types.contains(WebsiteDataType::DOMCache)) {
        if (m_cacheStorageManager && RefPtr { m_cacheStorageManager }->hasDataInMemory())
            result.add(WebsiteDataType::DOMCache);
    }

    return result;
}

OriginStorageManager::DataTypeSizeMap OriginStorageManager::StorageBucket::fetchDataTypesInListFromDisk(OptionSet<WebsiteDataType> types, bool shouldComputeSize)
{
    DataTypeSizeMap result;
    if (types.contains(WebsiteDataType::FileSystem)) {
        auto fileSystemStoragePath = typeStoragePath(StorageType::FileSystem);
        if (auto files = FileSystem::listDirectory(fileSystemStoragePath); !files.isEmpty()) {
            uint64_t size = 0;
            if (shouldComputeSize)
                size = valueOrDefault(FileSystem::directorySize(fileSystemStoragePath));
            result.add(WebsiteDataType::FileSystem, size);
        }
    }

    if (types.contains(WebsiteDataType::LocalStorage)) {
        auto localStoragePath = resolvedLocalStoragePath();
        if (FileSystem::fileExists(localStoragePath)) {
            uint64_t size = 0;
            if (shouldComputeSize)
                size = WebCore::SQLiteFileSystem::databaseFileSize(localStoragePath);
            result.add(WebsiteDataType::LocalStorage, size);
        }
    }

    if (types.contains(WebsiteDataType::IndexedDBDatabases)) {
        auto idbStoragePath = resolvedIDBStoragePath();
        if (auto databases = FileSystem::listDirectory(idbStoragePath); !databases.isEmpty()) {
            uint64_t size = 0;
            if (shouldComputeSize)
                size = valueOrDefault(FileSystem::directorySize(idbStoragePath));
            result.add(WebsiteDataType::IndexedDBDatabases, size);
        }
    }

    if (types.contains(WebsiteDataType::DOMCache)) {
        if (CacheStorageManager::hasCacheList(resolvedCacheStoragePath())) {
            uint64_t size = 0;
            if (shouldComputeSize)
                size = CacheStorageManager::cacheStorageSize(resolvedCacheStoragePath());
            result.add(WebsiteDataType::DOMCache, size);
        }
    }

    return result;
}

void OriginStorageManager::StorageBucket::deleteData(OptionSet<WebsiteDataType> types, WallTime modifiedSinceTime)
{
    if (types.contains(WebsiteDataType::FileSystem))
        deleteFileSystemStorageData(modifiedSinceTime);

    if (types.contains(WebsiteDataType::LocalStorage))
        deleteLocalStorageData(modifiedSinceTime);

    if (types.contains(WebsiteDataType::SessionStorage) && modifiedSinceTime < WallTime::now())
        deleteSessionStorageData();

    if (types.contains(WebsiteDataType::IndexedDBDatabases))
        deleteIDBStorageData(modifiedSinceTime);

    if (types.contains(WebsiteDataType::DOMCache))
        deleteCacheStorageData(modifiedSinceTime);
}

void OriginStorageManager::StorageBucket::deleteFileSystemStorageData(WallTime modifiedSinceTime)
{
    m_fileSystemStorageManager = nullptr;

    auto fileSystemStoragePath = typeStoragePath(StorageType::FileSystem);
    FileSystem::deleteAllFilesModifiedSince(fileSystemStoragePath, modifiedSinceTime);
}

void OriginStorageManager::StorageBucket::deleteLocalStorageData(WallTime time)
{
    auto currentLocalStoragePath = resolvedLocalStoragePath();
    if (FileSystem::fileModificationTime(currentLocalStoragePath) >= time) {
        if (m_localStorageManager)
            m_localStorageManager->clearDataOnDisk();
        WebCore::SQLiteFileSystem::deleteDatabaseFile(currentLocalStoragePath);
    }

    if (!m_localStorageManager)
        return;

    m_localStorageManager->clearDataInMemory();
    if (!m_localStorageManager->isActive())
        m_localStorageManager = nullptr;
}

void OriginStorageManager::StorageBucket::deleteSessionStorageData()
{
    if (!m_sessionStorageManager)
        return;

    m_sessionStorageManager->clearData();
    if (!m_sessionStorageManager->isActive())
        m_sessionStorageManager = nullptr;
}

void OriginStorageManager::StorageBucket::deleteIDBStorageData(WallTime time)
{
    if (m_idbStorageManager)
        m_idbStorageManager->closeDatabasesForDeletion();

    FileSystem::deleteAllFilesModifiedSince(resolvedIDBStoragePath(), time);
}

void OriginStorageManager::StorageBucket::deleteCacheStorageData(WallTime time)
{
    if (RefPtr manager = m_cacheStorageManager)
        manager->reset();

    FileSystem::deleteAllFilesModifiedSince(resolvedCacheStoragePath(), time);
}

void OriginStorageManager::StorageBucket::moveData(OptionSet<WebsiteDataType> types, const String& localStoragePath, const String& idbStoragePath)
{
    // This is only supported for IndexedDB and LocalStorage now.
    if (types.contains(WebsiteDataType::LocalStorage) && !localStoragePath.isEmpty()) {
        if (m_localStorageManager)
            m_localStorageManager->close();

        auto currentLocalStoragePath = resolvedLocalStoragePath();
        if (!currentLocalStoragePath.isEmpty()) {
            FileSystem::makeAllDirectories(FileSystem::parentPath(localStoragePath));
            WebCore::SQLiteFileSystem::moveDatabaseFile(currentLocalStoragePath, localStoragePath);
        }
    }

    if (types.contains(WebsiteDataType::IndexedDBDatabases) && !idbStoragePath.isEmpty()) {
        if (m_idbStorageManager)
            m_idbStorageManager->closeDatabasesForDeletion();

        auto currentIDBStoragePath = resolvedIDBStoragePath();
        if (!currentIDBStoragePath.isEmpty()) {
            FileSystem::makeAllDirectories(FileSystem::parentPath(idbStoragePath));
            FileSystem::moveFile(currentIDBStoragePath, idbStoragePath);
        }
    }
}

void OriginStorageManager::StorageBucket::deleteEmptyDirectory()
{
    switch (m_level) {
    case UnifiedOriginStorageLevel::None:
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::FileSystem));
        FileSystem::deleteEmptyDirectory(m_customLocalStoragePath);
        FileSystem::deleteEmptyDirectory(m_customIDBStoragePath);
        FileSystem::deleteEmptyDirectory(m_customCacheStoragePath);
        break;
    case UnifiedOriginStorageLevel::Basic:
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::FileSystem));
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::LocalStorage));
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::IndexedDB));
        FileSystem::deleteEmptyDirectory(m_customCacheStoragePath);
        break;
    case UnifiedOriginStorageLevel::Standard:
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::FileSystem));
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::LocalStorage));
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::IndexedDB));
        FileSystem::deleteEmptyDirectory(typeStoragePath(StorageType::CacheStorage));
    }
}

String OriginStorageManager::StorageBucket::resolvedLocalStoragePath()
{
    if (!m_resolvedLocalStoragePath.isNull())
        return m_resolvedLocalStoragePath;

    if (m_level == UnifiedOriginStorageLevel::None) {
        ASSERT(m_customLocalStoragePath.isEmpty() == m_rootPath.isEmpty());
        m_resolvedLocalStoragePath = m_customLocalStoragePath;
    } else if (!m_rootPath.isEmpty()) {
        auto localStorageDirectory = typeStoragePath(StorageType::LocalStorage);
        auto localStoragePath = LocalStorageManager::localStorageFilePath(localStorageDirectory);
        if (!m_customLocalStoragePath.isEmpty() && !FileSystem::fileExists(localStoragePath) && FileSystem::fileExists(m_customLocalStoragePath)) {
            RELEASE_LOG(Storage, "%p - StorageBucket::resolvedLocalStoragePath New path '%" PUBLIC_LOG_STRING "'", this, localStoragePath.utf8().data());
            FileSystem::makeAllDirectories(localStorageDirectory);
            auto moved = WebCore::SQLiteFileSystem::moveDatabaseFile(m_customLocalStoragePath, localStoragePath);
            if (!moved && !FileSystem::fileExists(localStoragePath))
                RELEASE_LOG_ERROR(Storage, "%p - StorageBucket::resolvedLocalStoragePath Fails to migrate file to new path", this);
        }

        m_resolvedLocalStoragePath = localStoragePath;
    } else
        m_resolvedLocalStoragePath = emptyString();

    return m_resolvedLocalStoragePath;
}

String OriginStorageManager::StorageBucket::resolvedIDBStoragePath()
{
    ASSERT(!RunLoop::isMain());

    if (!m_resolvedIDBStoragePath.isNull())
        return m_resolvedIDBStoragePath;

    if (m_level == UnifiedOriginStorageLevel::None) {
        ASSERT(m_customIDBStoragePath.isEmpty() == m_rootPath.isEmpty());
        m_resolvedIDBStoragePath = m_customIDBStoragePath;
    } else {
        auto idbStoragePath = typeStoragePath(StorageType::IndexedDB);
        auto moved = IDBStorageManager::migrateOriginData(m_customIDBStoragePath, idbStoragePath);
        if (!moved && FileSystem::fileExists(idbStoragePath)) {
            auto fileNames = FileSystem::listDirectory(m_customIDBStoragePath);
            auto newFileNames = FileSystem::listDirectory(idbStoragePath);
            RELEASE_LOG_ERROR(Storage, "%p - StorageBucket::resolvedLocalStoragePath Fails to migrate all databases to new path: %zu migrated, %zu left", this, newFileNames.size(), fileNames.size());
        }
        m_resolvedIDBStoragePath = idbStoragePath;
    }
    ASSERT(!m_resolvedIDBStoragePath.isNull());
    return m_resolvedIDBStoragePath;
}

String OriginStorageManager::StorageBucket::resolvedCacheStoragePath()
{
    if (!m_resolvedCacheStoragePath.isNull())
        return m_resolvedCacheStoragePath;

    switch (m_level) {
    case UnifiedOriginStorageLevel::None:
    case UnifiedOriginStorageLevel::Basic:
        ASSERT(m_customCacheStoragePath.isEmpty() == m_rootPath.isEmpty());
        m_resolvedCacheStoragePath = m_customCacheStoragePath;
        break;
    case UnifiedOriginStorageLevel::Standard:
        m_resolvedCacheStoragePath = typeStoragePath(StorageType::CacheStorage);
        if (!m_resolvedCacheStoragePath.isEmpty() && !m_customCacheStoragePath.isEmpty() && !FileSystem::fileExists(m_resolvedCacheStoragePath) && FileSystem::fileExists(m_customCacheStoragePath)) {
            RELEASE_LOG(Storage, "%p - StorageBucket::resolvedCacheStoragePath New path '%" PUBLIC_LOG_STRING "'", this, m_resolvedCacheStoragePath.utf8().data());
            FileSystem::moveFile(m_customCacheStoragePath, m_resolvedCacheStoragePath);
        }
    }

    return m_resolvedCacheStoragePath;
}

String OriginStorageManager::StorageBucket::resolvedBackgroundFetchStoragePath()
{
    if (m_resolvedCacheStoragePath.isNull())
        m_resolvedCacheStoragePath = typeStoragePath(StorageType::BackgroundFetchStorage);

    return m_resolvedCacheStoragePath;
}

String OriginStorageManager::StorageBucket::resolvedPath(WebsiteDataType webisteDataType)
{
    auto type = toStorageType(webisteDataType);
    if (!type)
        return { };

    switch (*type) {
    case StorageType::LocalStorage:
        return resolvedLocalStoragePath();
    case StorageType::IndexedDB:
        return resolvedIDBStoragePath();
    case StorageType::CacheStorage:
        return resolvedCacheStoragePath();
    case StorageType::BackgroundFetchStorage:
        return resolvedBackgroundFetchStoragePath();
    case StorageType::ServiceWorkerStorage:
    case StorageType::SessionStorage:
    case StorageType::FileSystem:
        return typeStoragePath(*type);
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void OriginStorageManager::StorageBucket::closeCacheStorageManager()
{
    m_cacheStorageManager = nullptr;
}

String OriginStorageManager::originFileIdentifier()
{
    return originFileName;
}

Ref<OriginQuotaManager> OriginStorageManager::createQuotaManager(OriginQuotaManager::Parameters&& parameters)
{
    OriginQuotaManager::GetUsageFunction getUsageFunction = [weakThis = WeakPtr { *this }]() -> uint64_t {
        CheckedPtr checkedThis = weakThis.get();
        if (!checkedThis)
            return 0;

        auto idbStoragePath = checkedThis->resolvedPath(WebsiteDataType::IndexedDBDatabases);
        auto cacheStoragePath = checkedThis->resolvedPath(WebsiteDataType::DOMCache);
        auto fileSystemStoragePath = checkedThis->resolvedPath(WebsiteDataType::FileSystem);
        uint64_t fileSystemStorageSize = valueOrDefault(FileSystem::directorySize(fileSystemStoragePath));
        if (RefPtr fileSystemStorageManager = checkedThis->existingFileSystemStorageManager()) {
            CheckedUint64 totalFileSystemStorageSize = fileSystemStorageSize;
            totalFileSystemStorageSize += fileSystemStorageManager->allocatedUnusedCapacity();
            if (!totalFileSystemStorageSize.hasOverflowed())
                fileSystemStorageSize = totalFileSystemStorageSize;
        }
        return IDBStorageManager::idbStorageSize(idbStoragePath) + CacheStorageManager::cacheStorageSize(cacheStoragePath) + fileSystemStorageSize;
    };

    return OriginQuotaManager::create(WTFMove(parameters), WTFMove(getUsageFunction));
}

OriginStorageManager::OriginStorageManager(OriginQuotaManager::Parameters&& parameters, String&& path, String&& customLocalStoragePath, String&& customIDBStoragePath, String&& customCacheStoragePath, UnifiedOriginStorageLevel level)
    : m_path(WTFMove(path))
    , m_customLocalStoragePath(WTFMove(customLocalStoragePath))
    , m_customIDBStoragePath(WTFMove(customIDBStoragePath))
    , m_customCacheStoragePath(WTFMove(customCacheStoragePath))
    , m_quotaManager(createQuotaManager(WTFMove(parameters)))
    , m_level(level)
{
    ASSERT(!RunLoop::isMain());
}

OriginStorageManager::~OriginStorageManager() = default;

void OriginStorageManager::connectionClosed(IPC::Connection::UniqueID connection)
{
    if (m_defaultBucket)
        m_defaultBucket->connectionClosed(connection);
}

OriginStorageManager::StorageBucket& OriginStorageManager::defaultBucket()
{
    if (!m_defaultBucket)
        m_defaultBucket = makeUnique<StorageBucket>(m_path, "default"_s, m_customLocalStoragePath, m_customIDBStoragePath, m_customCacheStoragePath, m_level);

    return *m_defaultBucket;
}

OriginQuotaManager& OriginStorageManager::quotaManager()
{
    return m_quotaManager.get();
}

Ref<OriginQuotaManager> OriginStorageManager::protectedQuotaManager()
{
    return m_quotaManager.get();
}

FileSystemStorageManager& OriginStorageManager::fileSystemStorageManager(FileSystemStorageHandleRegistry& registry)
{
    return defaultBucket().fileSystemStorageManager(registry, [quotaManager = ThreadSafeWeakPtr { this->quotaManager() }](uint64_t spaceRequested, CompletionHandler<void(bool)>&& completionHandler) mutable {
        auto strongReference = quotaManager.get();
        if (!strongReference)
            return completionHandler(false);

        strongReference->requestSpace(spaceRequested, [completionHandler = WTFMove(completionHandler)](auto decision) mutable {
            completionHandler(decision == OriginQuotaManager::Decision::Grant);
        });
    });
}

FileSystemStorageManager* OriginStorageManager::existingFileSystemStorageManager()
{
    return defaultBucket().existingFileSystemStorageManager();
}

LocalStorageManager& OriginStorageManager::localStorageManager(StorageAreaRegistry& registry)
{
    return defaultBucket().localStorageManager(registry);
}

LocalStorageManager* OriginStorageManager::existingLocalStorageManager()
{
    return defaultBucket().existingLocalStorageManager();
}

SessionStorageManager& OriginStorageManager::sessionStorageManager(StorageAreaRegistry& registry)
{
    return defaultBucket().sessionStorageManager(registry);
}

SessionStorageManager* OriginStorageManager::existingSessionStorageManager()
{
    return defaultBucket().existingSessionStorageManager();
}

IDBStorageManager& OriginStorageManager::idbStorageManager(IDBStorageRegistry& registry)
{
    return defaultBucket().idbStorageManager(registry, [quotaManager = ThreadSafeWeakPtr { this->quotaManager() }](uint64_t spaceRequested, CompletionHandler<void(bool)>&& completionHandler) mutable {
        auto strongReference = quotaManager.get();
        if (!strongReference)
            return completionHandler(false);

        strongReference->requestSpace(spaceRequested, [completionHandler = WTFMove(completionHandler)](auto decision) mutable {
            completionHandler(decision == OriginQuotaManager::Decision::Grant);
        });
    });
}

IDBStorageManager* OriginStorageManager::existingIDBStorageManager()
{
    return defaultBucket().existingIDBStorageManager();
}

CacheStorageManager* OriginStorageManager::existingCacheStorageManager()
{
    return defaultBucket().existingCacheStorageManager();
}

CacheStorageManager& OriginStorageManager::cacheStorageManager(CacheStorageRegistry& registry, const WebCore::ClientOrigin& origin, Ref<WorkQueue>&& queue)
{
    return defaultBucket().cacheStorageManager(registry, origin, [quotaManager = ThreadSafeWeakPtr { this->quotaManager() }](uint64_t spaceRequested, CompletionHandler<void(bool)>&& completionHandler) mutable {
        if (!quotaManager.get())
            return completionHandler(false);

        quotaManager.get()->requestSpace(spaceRequested, [completionHandler = WTFMove(completionHandler)](auto decision) mutable {
            completionHandler(decision == OriginQuotaManager::Decision::Grant);
        });
    }, WTFMove(queue));
}

Ref<CacheStorageManager> OriginStorageManager::protectedCacheStorageManager(CacheStorageRegistry& registry, const WebCore::ClientOrigin& origin, Ref<WorkQueue>&& queue)
{
    return cacheStorageManager(registry, origin, WTFMove(queue));
}

BackgroundFetchStoreManager& OriginStorageManager::backgroundFetchManager(Ref<WorkQueue>&& queue)
{
    return defaultBucket().backgroundFetchManager(WTFMove(queue), [quotaManager = ThreadSafeWeakPtr { this->quotaManager() }](uint64_t spaceRequested, CompletionHandler<void(bool)>&& completionHandler) mutable {
        if (!quotaManager.get())
            return completionHandler(false);

        quotaManager.get()->requestSpace(spaceRequested, [completionHandler = WTFMove(completionHandler)](auto decision) mutable {
            completionHandler(decision == OriginQuotaManager::Decision::Grant);
        });
    });
}

ServiceWorkerStorageManager& OriginStorageManager::serviceWorkerStorageManager()
{
    return defaultBucket().serviceWorkerStorageManager();
}

String OriginStorageManager::resolvedPath(WebsiteDataType type)
{
    return defaultBucket().resolvedPath(type);
}

bool OriginStorageManager::isActive()
{
    return defaultBucket().isActive();
}

bool OriginStorageManager::hasDataInMemory()
{
    return defaultBucket().hasDataInMemory();
}

bool OriginStorageManager::isEmpty()
{
    return defaultBucket().isEmpty();
}

WebCore::StorageEstimate OriginStorageManager::estimate()
{
    ASSERT(!RunLoop::isMain());

    Ref quotaManager = this->quotaManager();
    return WebCore::StorageEstimate { quotaManager->usage(), quotaManager->reportedQuota() };
}

OriginStorageManager::DataTypeSizeMap OriginStorageManager::fetchDataTypesInList(OptionSet<WebsiteDataType> types, bool shouldComputeSize)
{
    ASSERT(!RunLoop::isMain());

    return defaultBucket().fetchDataTypesInList(types, shouldComputeSize);
}

void OriginStorageManager::deleteData(OptionSet<WebsiteDataType> types, WallTime modifiedSince)
{
    ASSERT(!RunLoop::isMain());

    defaultBucket().deleteData(types, modifiedSince);
}

void OriginStorageManager::moveData(OptionSet<WebsiteDataType> types, const String& localStoragePath, const String& idbStoragePath)
{
    ASSERT(!RunLoop::isMain());

    defaultBucket().moveData(types, localStoragePath, idbStoragePath);
}

void OriginStorageManager::deleteEmptyDirectory()
{
    ASSERT(!RunLoop::isMain());

    if (m_path.isEmpty())
        return;

    defaultBucket().deleteEmptyDirectory();
}

void OriginStorageManager::closeCacheStorageManager()
{
    if (m_defaultBucket)
        m_defaultBucket->closeCacheStorageManager();
}

} // namespace WebKit

