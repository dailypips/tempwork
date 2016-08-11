// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_BUILDER_MAC_H_
#define CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_BUILDER_MAC_H_

#import <Foundation/Foundation.h>

#include "base/mac/scoped_nsobject.h"

@class NSUserNotification;

// Provides a marshallable way for storing the information required to construct
// a NSUSerNotification that is to be displayed on the system.
//
// A quick example:
//     base::scoped_nsobject<NotificationBuilder> builder(
//         [[NotificationBuilder alloc] init]);
//     [builder setTitle:@"Hello"];
//
//     // Build a notification out of the data.
//     NSUserNotification* notification =
//         [builder buildUserNotification];
//
//     // Serialize a notification out of the data.
//     NSDictionary* notificationData = [builder buildDictionary];
//
//     // Deserialize the |notificationData| in to a new builder.
//     base::scoped_nsobject<NotificationBuilder> finalBuilder(
//         [[NotificationBuilder alloc] initWithData:notificationData]);
@interface NotificationBuilder : NSObject

// Initializes an empty builder.
- (instancetype)init;

// Initializes a builder by deserializing |data|. The |data| must have been
// generated by calling the buildDictionary function on another builder
// instance.
- (instancetype)initWithDictionary:(NSDictionary*)data;

// Setters
// Note for XPC users. Always use the setters from Chrome's main app. Do not
// attempt to use them from XPC since some of the default strings and other
// defaults are not available from the xpc service.
- (void)setTitle:(NSString*)title;
- (void)setSubTitle:(NSString*)subTitle;
- (void)setContextMessage:(NSString*)contextMessage;
- (void)setIcon:(NSImage*)icon;
- (void)setButtons:(NSString*)primaryButton
    secondaryButton:(NSString*)secondaryButton;
- (void)setTag:(NSString*)tag;
- (void)setOrigin:(NSString*)origin;
- (void)setNotificationId:(NSString*)notificationId;
- (void)setProfileId:(NSString*)profileId;
- (void)setIncognito:(BOOL)incognito;
- (void)setNotificationType:(NSNumber*)notificationType;

// Returns a notification ready to be displayed out of the provided
// |notificationData|.
- (NSUserNotification*)buildUserNotification;

// Returns a representation of a notification that can be serialized.
// Another instance of NotificationBuilder can read this directly and generate
// a notification out of it via the |buildbuildUserNotification| method.
- (NSDictionary*)buildDictionary;

@end

#endif  // CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_NOTIFICATION_BUILDER_MAC_H_
