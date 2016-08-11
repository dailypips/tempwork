// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"

#include <memory>

#include "base/i18n/rtl.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"
#include "chrome/browser/ui/webui/ntp/core_app_launcher_handler.h"
#include "chrome/browser/ui/webui/ntp/favicon_webui_handler.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache_factory.h"
#include "chrome/common/url_constants.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_ui.h"
#include "extensions/browser/extension_system.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

#if defined(ENABLE_THEMES)
#include "chrome/browser/ui/webui/theme_handler.h"
#endif

using content::BrowserThread;
using content::WebUIController;

namespace {

// Strings sent to the page via jstemplates used to set the direction of the
// HTML document based on locale.
const char kRTLHtmlTextDirection[] = "rtl";
const char kLTRHtmlTextDirection[] = "ltr";

const char* GetHtmlTextDirection(const base::string16& text) {
  if (base::i18n::IsRTL() && base::i18n::StringContainsStrongRTLChars(text))
    return kRTLHtmlTextDirection;
  else
    return kLTRHtmlTextDirection;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// NewTabUI

NewTabUI::NewTabUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  web_ui->OverrideTitle(l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE));

  Profile* profile = GetProfile();
  if (!profile->IsOffTheRecord()) {
    web_ui->AddMessageHandler(new MetricsHandler());
    web_ui->AddMessageHandler(new FaviconWebUIHandler());
    web_ui->AddMessageHandler(new CoreAppLauncherHandler());

    ExtensionService* service =
        extensions::ExtensionSystem::Get(profile)->extension_service();
    // We might not have an ExtensionService (on ChromeOS when not logged in
    // for example).
    if (service)
      web_ui->AddMessageHandler(new AppLauncherHandler(service));
  }

#if defined(ENABLE_THEMES)
  if (!profile->IsGuestSession())
    web_ui->AddMessageHandler(new ThemeHandler());
#endif

  std::unique_ptr<NewTabHTMLSource> html_source(
      new NewTabHTMLSource(profile->GetOriginalProfile()));

  // content::URLDataSource assumes the ownership of the html_source.
  content::URLDataSource::Add(profile, html_source.release());

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(bookmarks::prefs::kShowBookmarkBar,
                             base::Bind(&NewTabUI::OnShowBookmarkBarChanged,
                                        base::Unretained(this)));
}

NewTabUI::~NewTabUI() {}

void NewTabUI::OnShowBookmarkBarChanged() {
  base::StringValue attached(
      GetProfile()->GetPrefs()->GetBoolean(bookmarks::prefs::kShowBookmarkBar) ?
          "true" : "false");
  web_ui()->CallJavascriptFunctionUnsafe("ntp.setBookmarkBarAttached",
                                         attached);
}

// static
void NewTabUI::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  CoreAppLauncherHandler::RegisterProfilePrefs(registry);
  AppLauncherHandler::RegisterProfilePrefs(registry);
}

// static
bool NewTabUI::IsNewTab(const GURL& url) {
  return url.GetOrigin() == GURL(chrome::kChromeUINewTabURL).GetOrigin();
}

// static
bool NewTabUI::ShouldShowApps() {
// Ash shows apps in app list thus should not show apps page in NTP4.
#if defined(USE_ASH)
  return false;
#else
  return true;
#endif
}

// static
void NewTabUI::SetUrlTitleAndDirection(base::DictionaryValue* dictionary,
                                       const base::string16& title,
                                       const GURL& gurl) {
  dictionary->SetString("url", gurl.spec());

  bool using_url_as_the_title = false;
  base::string16 title_to_set(title);
  if (title_to_set.empty()) {
    using_url_as_the_title = true;
    title_to_set = base::UTF8ToUTF16(gurl.spec());
  }

  // We set the "dir" attribute of the title, so that in RTL locales, a LTR
  // title is rendered left-to-right and truncated from the right. For example,
  // the title of http://msdn.microsoft.com/en-us/default.aspx is "MSDN:
  // Microsoft developer network". In RTL locales, in the [New Tab] page, if
  // the "dir" of this title is not specified, it takes Chrome UI's
  // directionality. So the title will be truncated as "soft developer
  // network". Setting the "dir" attribute as "ltr" renders the truncated title
  // as "MSDN: Microsoft D...". As another example, the title of
  // http://yahoo.com is "Yahoo!". In RTL locales, in the [New Tab] page, the
  // title will be rendered as "!Yahoo" if its "dir" attribute is not set to
  // "ltr".
  std::string direction;
  if (using_url_as_the_title)
    direction = kLTRHtmlTextDirection;
  else
    direction = GetHtmlTextDirection(title);

  dictionary->SetString("title", title_to_set);
  dictionary->SetString("direction", direction);
}

// static
void NewTabUI::SetFullNameAndDirection(const base::string16& full_name,
                                       base::DictionaryValue* dictionary) {
  dictionary->SetString("full_name", full_name);
  dictionary->SetString("full_name_direction", GetHtmlTextDirection(full_name));
}

Profile* NewTabUI::GetProfile() const {
  return Profile::FromWebUI(web_ui());
}

///////////////////////////////////////////////////////////////////////////////
// NewTabHTMLSource

NewTabUI::NewTabHTMLSource::NewTabHTMLSource(Profile* profile)
    : profile_(profile) {
}

std::string NewTabUI::NewTabHTMLSource::GetSource() const {
  return chrome::kChromeUINewTabHost;
}

void NewTabUI::NewTabHTMLSource::StartDataRequest(
    const std::string& path,
    int render_process_id,
    int render_frame_id,
    const content::URLDataSource::GotDataCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::map<std::string, std::pair<std::string, int> >::iterator it =
    resource_map_.find(path);
  if (it != resource_map_.end()) {
    scoped_refptr<base::RefCountedMemory> resource_bytes(
        it->second.second ?
            ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
                it->second.second) :
            new base::RefCountedStaticMemory);
    callback.Run(resource_bytes.get());
    return;
  }

  if (!path.empty() && path[0] != '#') {
    // A path under new-tab was requested; it's likely a bad relative
    // URL from the new tab page, but in any case it's an error.
    NOTREACHED() << path << " should not have been requested on the NTP";
    callback.Run(NULL);
    return;
  }

  content::RenderProcessHost* render_host =
      content::RenderProcessHost::FromID(render_process_id);
  NTPResourceCache::WindowType win_type = NTPResourceCache::GetWindowType(
      profile_, render_host);
  scoped_refptr<base::RefCountedMemory> html_bytes(
      NTPResourceCacheFactory::GetForProfile(profile_)->
      GetNewTabHTML(win_type));

  callback.Run(html_bytes.get());
}

std::string NewTabUI::NewTabHTMLSource::GetMimeType(const std::string& resource)
    const {
  std::map<std::string, std::pair<std::string, int> >::const_iterator it =
      resource_map_.find(resource);
  if (it != resource_map_.end())
    return it->second.first;
  return "text/html";
}

bool NewTabUI::NewTabHTMLSource::ShouldReplaceExistingSource() const {
  return false;
}

std::string NewTabUI::NewTabHTMLSource::GetContentSecurityPolicyScriptSrc()
    const {
  // 'unsafe-inline' and google resources are added to script-src.
  return "script-src chrome://resources 'self' 'unsafe-eval' 'unsafe-inline' "
      "*.google.com *.gstatic.com;";
}

std::string NewTabUI::NewTabHTMLSource::GetContentSecurityPolicyStyleSrc()
    const {
  return "style-src 'self' chrome://resources 'unsafe-inline' chrome://theme;";
}

std::string NewTabUI::NewTabHTMLSource::GetContentSecurityPolicyImgSrc()
    const {
  return "img-src chrome-search://thumb chrome-search://thumb2 "
      "chrome-search://theme chrome://theme data:;";
}

std::string NewTabUI::NewTabHTMLSource::GetContentSecurityPolicyChildSrc()
    const {
  return "child-src chrome-search://most-visited;";
}

void NewTabUI::NewTabHTMLSource::AddResource(const char* resource,
                                             const char* mime_type,
                                             int resource_id) {
  DCHECK(resource);
  DCHECK(mime_type);
  resource_map_[std::string(resource)] =
      std::make_pair(std::string(mime_type), resource_id);
}

NewTabUI::NewTabHTMLSource::~NewTabHTMLSource() {}
