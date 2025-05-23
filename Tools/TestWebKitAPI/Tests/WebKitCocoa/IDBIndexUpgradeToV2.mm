/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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

#import "config.h"

#import "DeprecatedGlobalValues.h"
#import "PlatformUtilities.h"
#import "Test.h"
#import <WebCore/SQLiteFileSystem.h>
#import <WebKit/WKProcessPoolPrivate.h>
#import <WebKit/WKUserContentControllerPrivate.h>
#import <WebKit/WKWebViewConfigurationPrivate.h>
#import <WebKit/WKWebsiteDataStorePrivate.h>
#import <WebKit/WebKit.h>
#import <WebKit/_WKProcessPoolConfiguration.h>
#import <WebKit/_WKUserStyleSheet.h>
#import <wtf/RetainPtr.h>

@interface IDBIndexUpgradeToV2MessageHandler : NSObject <WKScriptMessageHandler>
@end

@implementation IDBIndexUpgradeToV2MessageHandler

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message
{
    receivedScriptMessage = true;
    lastScriptMessage = message;
}

@end

TEST(IndexedDB, IndexUpgradeToV2)
{
    RetainPtr<IDBIndexUpgradeToV2MessageHandler> handler = adoptNS([[IDBIndexUpgradeToV2MessageHandler alloc] init]);
    RetainPtr<WKWebViewConfiguration> configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];

    [configuration.get().websiteDataStore _terminateNetworkProcess];

    // Copy the inconsistent database files to the database directory
    NSURL *url1 = [NSBundle.test_resourcesBundle URLForResource:@"IndexUpgrade" withExtension:@"sqlite3"];
    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"IndexUpgrade" withExtension:@"blob"];

    RetainPtr hash = WebCore::SQLiteFileSystem::computeHashForFileName("index-upgrade-test"_s).createNSString();
    NSString *originDirectory = @"~/Library/WebKit/com.apple.WebKit.TestWebKitAPI/WebsiteData/IndexedDB/v1/file__0/";
    NSString *databaseDirectory = [[originDirectory stringByAppendingString:hash.get()] stringByExpandingTildeInPath];
    NSURL *targetURL = [NSURL fileURLWithPath:databaseDirectory];
    [[NSFileManager defaultManager] removeItemAtURL:targetURL error:nil];
    [[NSFileManager defaultManager] createDirectoryAtURL:targetURL withIntermediateDirectories:YES attributes:nil error:nil];

    [[NSFileManager defaultManager] copyItemAtURL:url1 toURL:[targetURL URLByAppendingPathComponent:@"IndexedDB.sqlite3"] error:nil];
    [[NSFileManager defaultManager] copyItemAtURL:url2 toURL:[targetURL URLByAppendingPathComponent:@"1.blob"] error:nil];

    RetainPtr<WKWebView> webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    NSURLRequest *request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IDBIndexUpgradeToV2" withExtension:@"html"]];
    [webView loadRequest:request];

    TestWebKitAPI::Util::run(&receivedScriptMessage);

    EXPECT_WK_STREQ(@"Object expected to be a blob: [object Blob]", [lastScriptMessage body]);
}

static void runMultipleIndicesTestWithDatabase(NSString* databaseName)
{
    RetainPtr handler = adoptNS([[IDBIndexUpgradeToV2MessageHandler alloc] init]);
    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addScriptMessageHandler:handler.get() name:@"testHandler"];
    [configuration.get().websiteDataStore _terminateNetworkProcess];

    RetainPtr url = [NSBundle.test_resourcesBundle URLForResource:databaseName withExtension:@"sqlite3"];
    String hash = WebCore::SQLiteFileSystem::computeHashForFileName("index-upgrade-test"_s);
    RetainPtr originURL = [NSURL URLWithString:@"file://"];
    __block RetainPtr<NSString> originDirectoryString;
    [configuration.get().websiteDataStore _originDirectoryForTesting:originURL.get() topOrigin:originURL.get() type:WKWebsiteDataTypeIndexedDBDatabases completionHandler:^(NSString *result) {
        originDirectoryString = result;
        done = true;
    }];
    TestWebKitAPI::Util::run(&done);
    RetainPtr databaseDirectory = [[NSURL fileURLWithPath:originDirectoryString.get() isDirectory:YES] URLByAppendingPathComponent:hash.createNSString().get()];
    [[NSFileManager defaultManager] removeItemAtURL:databaseDirectory.get() error:nil];
    [[NSFileManager defaultManager] createDirectoryAtURL:databaseDirectory.get() withIntermediateDirectories:YES attributes:nil error:nil];
    [[NSFileManager defaultManager] copyItemAtURL:url.get() toURL:[databaseDirectory URLByAppendingPathComponent:@"IndexedDB.sqlite3"] error:nil];

    RetainPtr webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    RetainPtr request = [NSURLRequest requestWithURL:[NSBundle.test_resourcesBundle URLForResource:@"IDBIndexUpgradeToV2WithMultipleIndices" withExtension:@"html"]];
    [webView loadRequest:request.get()];
    TestWebKitAPI::Util::run(&receivedScriptMessage);

    EXPECT_WK_STREQ(@"Get object: {\"name\":\"apple\",\"color\":\"red\"}", [lastScriptMessage body]);
}

TEST(IndexedDB, IndexUpgradeToV2WithMultipleIndices)
{
    runMultipleIndicesTestWithDatabase(@"IndexUpgradeWithMultipleIndices");
}
