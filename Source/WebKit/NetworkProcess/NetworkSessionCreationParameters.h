/*
 * Copyright (C) 2017-2021 Apple Inc. All rights reserved.
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

#pragma once

#include "ResourceLoadStatisticsParameters.h"
#include "UnifiedOriginStorageLevel.h"
#include "WebPushDaemonConnectionConfiguration.h"
#include <WebCore/NetworkStorageSession.h>
#include <pal/SessionID.h>
#include <wtf/Seconds.h>
#include <wtf/URL.h>
#include <wtf/UUID.h>

#if USE(SOUP)
#include "SoupCookiePersistentStorageType.h"
#include <WebCore/HTTPCookieAcceptPolicy.h>
#include <WebCore/SoupNetworkProxySettings.h>
#endif

#if USE(CURL)
#include <WebCore/CurlProxySettings.h>
#endif

namespace WebKit {

enum class AllowsCellularAccess : bool { No, Yes };

struct NetworkSessionCreationParameters {
    PAL::SessionID sessionID { PAL::SessionID::defaultSessionID() };
    Markable<WTF::UUID> dataStoreIdentifier;
    String boundInterfaceIdentifier;
    AllowsCellularAccess allowsCellularAccess { AllowsCellularAccess::Yes };
#if PLATFORM(COCOA)
    RetainPtr<CFDictionaryRef> proxyConfiguration;
    String sourceApplicationBundleIdentifier;
    String sourceApplicationSecondaryIdentifier;
    bool shouldLogCookieInformation { false };
    URL httpProxy;
    URL httpsProxy;
#endif
#if HAVE(ALTERNATIVE_SERVICE)
    String alternativeServiceDirectory;
    SandboxExtension::Handle alternativeServiceDirectoryExtensionHandle;
#endif
    String hstsStorageDirectory;
    SandboxExtension::Handle hstsStorageDirectoryExtensionHandle;
#if USE(SOUP)
    String cookiePersistentStoragePath;
    SoupCookiePersistentStorageType cookiePersistentStorageType { SoupCookiePersistentStorageType::Text };
    bool persistentCredentialStorageEnabled { true };
    bool ignoreTLSErrors { false };
    WebCore::SoupNetworkProxySettings proxySettings;
    WebCore::HTTPCookieAcceptPolicy cookieAcceptPolicy { WebCore::HTTPCookieAcceptPolicy::ExclusivelyFromMainDocumentDomain };
#endif
#if USE(CURL)
    String cookiePersistentStorageFile;
    WebCore::CurlProxySettings proxySettings;
#endif
    bool deviceManagementRestrictionsEnabled { false };
    bool allLoadsBlockedByDeviceManagementRestrictionsForTesting { false };
    WebPushD::WebPushDaemonConnectionConfiguration webPushDaemonConnectionConfiguration;

    String networkCacheDirectory;
    SandboxExtension::Handle networkCacheDirectoryExtensionHandle;
    String dataConnectionServiceType;
    bool fastServerTrustEvaluationEnabled { false };
    bool networkCacheSpeculativeValidationEnabled { false };
    bool shouldUseTestingNetworkSession { false };
    bool staleWhileRevalidateEnabled { false };
    unsigned testSpeedMultiplier { 1 };
    bool suppressesConnectionTerminationOnSystemChange { false };
    bool allowsServerPreconnect { true };
    bool requiresSecureHTTPSProxyConnection { false };
    bool shouldRunServiceWorkersOnMainThreadForTesting { false };
    std::optional<unsigned> overrideServiceWorkerRegistrationCountTestingValue;
    bool preventsSystemHTTPProxyAuthentication { false };
    std::optional<bool> useNetworkLoader { std::nullopt };
    bool allowsHSTSWithUntrustedRootCertificate { false };
    String pcmMachServiceName;
    String webPushMachServiceName;
    String webPushPartitionString;
    bool enablePrivateClickMeasurementDebugMode { false };
    bool isBlobRegistryTopOriginPartitioningEnabled { false };
    bool isOptInCookiePartitioningEnabled { false };
    bool shouldSendPrivateTokenIPCForTesting { false };
    uint64_t cookiesVersion { 0 };

    UnifiedOriginStorageLevel unifiedOriginStorageLevel { UnifiedOriginStorageLevel::Standard };
    uint64_t perOriginStorageQuota;
    std::optional<double> originQuotaRatio;
    std::optional<double> totalQuotaRatio;
    std::optional<uint64_t> standardVolumeCapacity;
    std::optional<uint64_t> volumeCapacityOverride;
    String localStorageDirectory;
    SandboxExtension::Handle localStorageDirectoryExtensionHandle;
    String indexedDBDirectory;
    SandboxExtension::Handle indexedDBDirectoryExtensionHandle;
    String cacheStorageDirectory;
    SandboxExtension::Handle cacheStorageDirectoryExtensionHandle;
    String generalStorageDirectory;
    SandboxExtension::Handle generalStorageDirectoryHandle;
    String serviceWorkerRegistrationDirectory;
    SandboxExtension::Handle serviceWorkerRegistrationDirectoryExtensionHandle;
    bool serviceWorkerProcessTerminationDelayEnabled { true };
    bool inspectionForServiceWorkersAllowed { true };
    bool storageSiteValidationEnabled { false };
#if ENABLE(DECLARATIVE_WEB_PUSH)
    bool isDeclarativeWebPushEnabled { false };
#endif
#if HAVE(NW_PROXY_CONFIG)
    std::optional<Vector<std::pair<Vector<uint8_t>, std::optional<WTF::UUID>>>> proxyConfigData;
#endif
    ResourceLoadStatisticsParameters resourceLoadStatisticsParameters;

#if ENABLE(CONTENT_EXTENSIONS)
    String resourceMonitorThrottlerDirectory;
    SandboxExtension::Handle resourceMonitorThrottlerDirectoryExtensionHandle;
#endif
#if ENABLE(TLS_1_2_DEFAULT_MINIMUM)
    bool isLegacyTLSAllowed { false };
#else
    bool isLegacyTLSAllowed { true };
#endif
#if HAVE(WEBCONTENTRESTRICTIONS_PATH_SPI)
    String webContentRestrictionsConfigurationFile;
    SandboxExtension::Handle webContentRestrictionsConfigurationExtensionHandle;
#endif
};

} // namespace WebKit
