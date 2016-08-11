// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SIGNIN_VIEW_CONTROLLER_DELEGATE_H_
#define CHROME_BROWSER_UI_SIGNIN_VIEW_CONTROLLER_DELEGATE_H_

#include "chrome/browser/ui/profile_chooser_constants.h"
#include "content/public/browser/web_contents_delegate.h"

class Browser;
class ModalSigninDelegate;
class SigninViewController;

namespace signin_metrics {
enum class AccessPoint;
}

// Abstract base class to the platform-specific managers of the Signin and Sync
// confirmation tab-modal dialogs. This and its platform-specific
// implementations are responsible for actually creating and owning the dialogs,
// as well as managing the navigation inside them.
// Subclasses are responsible for deleting themselves when the window they're
// managing closes.
class SigninViewControllerDelegate : public content::WebContentsDelegate {
 public:
  static SigninViewControllerDelegate* CreateModalSigninDelegate(
      SigninViewController* signin_view_controller,
      profiles::BubbleViewMode mode,
      Browser* browser,
      signin_metrics::AccessPoint access_point);

  static SigninViewControllerDelegate* CreateSyncConfirmationDelegate(
      SigninViewController* signin_view_controller,
      Browser* browser);

  void CloseModalSignin();

  // Either navigates back in the signin flow if the history state allows it or
  // closes the flow otherwise.
  void PerformNavigation();

  // This will be called by the base class to request a resize of the native
  // view hosting the content to |height|. |height| is the total height of the
  // content, in pixels.
  virtual void ResizeNativeView(int height) = 0;

  // content::WebContentsDelegate:
  bool HandleContextMenu(const content::ContextMenuParams& params) override;

  // WebContents is used for executing javascript in the context of a modal sync
  // confirmation dialog.
  content::WebContents* web_contents_for_testing() { return web_contents_; }

 protected:
  SigninViewControllerDelegate(SigninViewController* signin_view_controller,
                               content::WebContents* web_contents);
  ~SigninViewControllerDelegate() override;

  // Notifies the SigninViewController that this instance is being deleted.
  void ResetSigninViewControllerDelegate();

  // content::WebContentsDelegate
  void LoadingStateChanged(content::WebContents* source,
                           bool to_different_document) override;

  // This will be called by this base class when the tab-modal window must be
  // closed. This should close the platform-specific window that is currently
  // showing the sign in flow or the sync confirmation dialog.
  virtual void PerformClose() = 0;

 private:
  bool CanGoBack(content::WebContents* web_ui_web_contents) const;

  SigninViewController* signin_view_controller_;  // Not owned.
  content::WebContents* web_contents_;  // Not owned.
  DISALLOW_COPY_AND_ASSIGN(SigninViewControllerDelegate);
};

#endif  // CHROME_BROWSER_UI_SIGNIN_VIEW_CONTROLLER_DELEGATE_H_
