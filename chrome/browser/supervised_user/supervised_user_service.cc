// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/supervised_user/supervised_user_service.h"

#include <utility>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_runner_util.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/component_updater/supervised_user_whitelist_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/supervised_user/experimental/supervised_user_filtering_switches.h"
#include "chrome/browser/supervised_user/permission_request_creator.h"
#include "chrome/browser/supervised_user/supervised_user_constants.h"
#include "chrome/browser/supervised_user/supervised_user_features.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_service_observer.h"
#include "chrome/browser/supervised_user/supervised_user_settings_service.h"
#include "chrome/browser/supervised_user/supervised_user_settings_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_site_list.h"
#include "chrome/browser/supervised_user/supervised_user_whitelist_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/browser_sync/browser/profile_sync_service.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_manager_base.h"
#include "components/signin/core/common/signin_switches.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/user_metrics.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(OS_ANDROID)
#include "chrome/browser/supervised_user/legacy/custodian_profile_downloader_service.h"
#include "chrome/browser/supervised_user/legacy/custodian_profile_downloader_service_factory.h"
#include "chrome/browser/supervised_user/legacy/permission_request_creator_sync.h"
#include "chrome/browser/supervised_user/legacy/supervised_user_pref_mapping_service.h"
#include "chrome/browser/supervised_user/legacy/supervised_user_pref_mapping_service_factory.h"
#include "chrome/browser/supervised_user/legacy/supervised_user_registration_utility.h"
#include "chrome/browser/supervised_user/legacy/supervised_user_shared_settings_service_factory.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/supervised_user_manager.h"
#include "components/user_manager/user_manager.h"
#endif

#if defined(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#endif

#if defined(ENABLE_THEMES)
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#endif

using base::DictionaryValue;
using base::UserMetricsAction;
using content::BrowserThread;

#if defined(ENABLE_EXTENSIONS)
using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::ExtensionRegistry;
using extensions::ExtensionSystem;
#endif

#if defined(ENABLE_EXTENSIONS)
using extensions::ExtensionPrefs;
#endif

namespace {

// The URL from which to download a host blacklist if no local one exists yet.
const char kBlacklistURL[] =
    "https://www.gstatic.com/chrome/supervised_user/blacklist-20141001-1k.bin";
// The filename under which we'll store the blacklist (in the user data dir).
const char kBlacklistFilename[] = "su-blacklist.bin";

const char* const kCustodianInfoPrefs[] = {
  prefs::kSupervisedUserCustodianName,
  prefs::kSupervisedUserCustodianEmail,
  prefs::kSupervisedUserCustodianProfileImageURL,
  prefs::kSupervisedUserCustodianProfileURL,
  prefs::kSupervisedUserSecondCustodianName,
  prefs::kSupervisedUserSecondCustodianEmail,
  prefs::kSupervisedUserSecondCustodianProfileImageURL,
  prefs::kSupervisedUserSecondCustodianProfileURL,
};

void CreateURLAccessRequest(
    const GURL& url,
    PermissionRequestCreator* creator,
    const SupervisedUserService::SuccessCallback& callback) {
  creator->CreateURLAccessRequest(url, callback);
}

void CreateExtensionInstallRequest(
    const std::string& id,
    PermissionRequestCreator* creator,
    const SupervisedUserService::SuccessCallback& callback) {
  creator->CreateExtensionInstallRequest(id, callback);
}

void CreateExtensionUpdateRequest(
    const std::string& id,
    PermissionRequestCreator* creator,
    const SupervisedUserService::SuccessCallback& callback) {
  creator->CreateExtensionUpdateRequest(id, callback);
}

// Default callback for AddExtensionInstallRequest.
void ExtensionInstallRequestSent(const std::string& id, bool success) {
  VLOG_IF(1, !success) << "Failed sending install request for " << id;
}

// Default callback for AddExtensionUpdateRequest.
void ExtensionUpdateRequestSent(const std::string& id, bool success) {
  VLOG_IF(1, !success) << "Failed sending update request for " << id;
}

base::FilePath GetBlacklistPath() {
  base::FilePath blacklist_dir;
  PathService::Get(chrome::DIR_USER_DATA, &blacklist_dir);
  return blacklist_dir.AppendASCII(kBlacklistFilename);
}

}  // namespace

SupervisedUserService::~SupervisedUserService() {
  DCHECK(!did_init_ || did_shutdown_);
  url_filter_context_.ui_url_filter()->RemoveObserver(this);
}

// static
void SupervisedUserService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(prefs::kSupervisedUserApprovedExtensions);
  registry->RegisterDictionaryPref(prefs::kSupervisedUserManualHosts);
  registry->RegisterDictionaryPref(prefs::kSupervisedUserManualURLs);
  registry->RegisterIntegerPref(prefs::kDefaultSupervisedUserFilteringBehavior,
                                SupervisedUserURLFilter::ALLOW);
  registry->RegisterBooleanPref(prefs::kSupervisedUserCreationAllowed, true);
  registry->RegisterBooleanPref(prefs::kSupervisedUserSafeSites, true);
  for (const char* pref : kCustodianInfoPrefs) {
    registry->RegisterStringPref(pref, std::string());
  }
}

void SupervisedUserService::Init() {
  DCHECK(!did_init_);
  did_init_ = true;
  DCHECK(GetSettingsService()->IsReady());

  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kSupervisedUserId,
      base::Bind(&SupervisedUserService::OnSupervisedUserIdChanged,
          base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kForceSessionSync,
      base::Bind(&SupervisedUserService::OnForceSessionSyncChanged,
                 base::Unretained(this)));

  ProfileSyncService* sync_service =
      ProfileSyncServiceFactory::GetForProfile(profile_);
  // Can be null in tests.
  if (sync_service)
    sync_service->AddPreferenceProvider(this);

  std::string client_id = component_updater::SupervisedUserWhitelistInstaller::
      ClientIdForProfilePath(profile_->GetPath());
  whitelist_service_.reset(new SupervisedUserWhitelistService(
      profile_->GetPrefs(),
      g_browser_process->supervised_user_whitelist_installer(), client_id));
  whitelist_service_->AddSiteListsChangedCallback(
      base::Bind(&SupervisedUserService::OnSiteListsChanged,
                 weak_ptr_factory_.GetWeakPtr()));

  SetActive(ProfileIsSupervised());
}

void SupervisedUserService::SetDelegate(Delegate* delegate) {
  if (delegate) {
    // Changing delegates isn't allowed.
    DCHECK(!delegate_);
  } else {
    // If the delegate is removed, deactivate first to give the old delegate a
    // chance to clean up.
    SetActive(false);
  }
  delegate_ = delegate;
}

scoped_refptr<const SupervisedUserURLFilter>
SupervisedUserService::GetURLFilterForIOThread() {
  return url_filter_context_.io_url_filter();
}

SupervisedUserURLFilter* SupervisedUserService::GetURLFilterForUIThread() {
  return url_filter_context_.ui_url_filter();
}

SupervisedUserWhitelistService* SupervisedUserService::GetWhitelistService() {
  return whitelist_service_.get();
}

bool SupervisedUserService::AccessRequestsEnabled() {
  return FindEnabledPermissionRequestCreator(0) < permissions_creators_.size();
}

void SupervisedUserService::AddURLAccessRequest(
    const GURL& url,
    const SuccessCallback& callback) {
  AddPermissionRequestInternal(
      base::Bind(CreateURLAccessRequest,
                 SupervisedUserURLFilter::Normalize(url)),
      callback, 0);
}

void SupervisedUserService::ReportURL(const GURL& url,
                                      const SuccessCallback& callback) {
  if (url_reporter_)
    url_reporter_->ReportUrl(url, callback);
  else
    callback.Run(false);
}

void SupervisedUserService::AddExtensionInstallRequest(
    const std::string& extension_id,
    const base::Version& version,
    const SuccessCallback& callback) {
  std::string id = GetExtensionRequestId(extension_id, version);
  AddPermissionRequestInternal(base::Bind(CreateExtensionInstallRequest, id),
                               callback, 0);
}

void SupervisedUserService::AddExtensionInstallRequest(
    const std::string& extension_id,
    const base::Version& version) {
  std::string id = GetExtensionRequestId(extension_id, version);
  AddPermissionRequestInternal(base::Bind(CreateExtensionInstallRequest, id),
                               base::Bind(ExtensionInstallRequestSent, id), 0);
}

void SupervisedUserService::AddExtensionUpdateRequest(
    const std::string& extension_id,
    const base::Version& version,
    const SuccessCallback& callback) {
  std::string id = GetExtensionRequestId(extension_id, version);
  AddPermissionRequestInternal(
      base::Bind(CreateExtensionUpdateRequest, id), callback, 0);
}

void SupervisedUserService::AddExtensionUpdateRequest(
    const std::string& extension_id,
    const base::Version& version) {
  std::string id = GetExtensionRequestId(extension_id, version);
  AddExtensionUpdateRequest(extension_id, version,
                            base::Bind(ExtensionUpdateRequestSent, id));
}

// static
std::string SupervisedUserService::GetExtensionRequestId(
    const std::string& extension_id,
    const base::Version& version) {
  return base::StringPrintf("%s:%s", extension_id.c_str(),
                            version.GetString().c_str());
}

std::string SupervisedUserService::GetCustodianEmailAddress() const {
  std::string email = profile_->GetPrefs()->GetString(
      prefs::kSupervisedUserCustodianEmail);
#if defined(OS_CHROMEOS)
  // |GetActiveUser()| can return null in unit tests.
  if (email.empty() && !!user_manager::UserManager::Get()->GetActiveUser()) {
    email = chromeos::ChromeUserManager::Get()
        ->GetSupervisedUserManager()
        ->GetManagerDisplayEmail(
            user_manager::UserManager::Get()->GetActiveUser()->email());
  }
#endif
  return email;
}

std::string SupervisedUserService::GetCustodianName() const {
  std::string name = profile_->GetPrefs()->GetString(
      prefs::kSupervisedUserCustodianName);
#if defined(OS_CHROMEOS)
  // |GetActiveUser()| can return null in unit tests.
  if (name.empty() && !!user_manager::UserManager::Get()->GetActiveUser()) {
    name = base::UTF16ToUTF8(chromeos::ChromeUserManager::Get()
        ->GetSupervisedUserManager()
        ->GetManagerDisplayName(
            user_manager::UserManager::Get()->GetActiveUser()->email()));
  }
#endif
  return name.empty() ? GetCustodianEmailAddress() : name;
}

std::string SupervisedUserService::GetSecondCustodianEmailAddress() const {
  return profile_->GetPrefs()->GetString(
      prefs::kSupervisedUserSecondCustodianEmail);
}

std::string SupervisedUserService::GetSecondCustodianName() const {
  std::string name = profile_->GetPrefs()->GetString(
      prefs::kSupervisedUserSecondCustodianName);
  return name.empty() ? GetSecondCustodianEmailAddress() : name;
}

base::string16 SupervisedUserService::GetExtensionsLockedMessage() const {
  return l10n_util::GetStringFUTF16(IDS_EXTENSIONS_LOCKED_SUPERVISED_USER,
                                    base::UTF8ToUTF16(GetCustodianName()));
}

#if !defined(OS_ANDROID)
void SupervisedUserService::InitSync(const std::string& refresh_token) {
  StartSetupSync();

  ProfileOAuth2TokenService* token_service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(profile_);
  token_service->UpdateCredentials(supervised_users::kSupervisedUserPseudoEmail,
                                   refresh_token);

  FinishSetupSyncWhenReady();
}

void SupervisedUserService::RegisterAndInitSync(
    SupervisedUserRegistrationUtility* registration_utility,
    Profile* custodian_profile,
    const std::string& supervised_user_id,
    const AuthErrorCallback& callback) {
  DCHECK(ProfileIsSupervised());
  DCHECK(!custodian_profile->IsSupervised());

  base::string16 name = base::UTF8ToUTF16(
      profile_->GetPrefs()->GetString(prefs::kProfileName));
  int avatar_index = profile_->GetPrefs()->GetInteger(
      prefs::kProfileAvatarIndex);
  SupervisedUserRegistrationInfo info(name, avatar_index);
  registration_utility->Register(
      supervised_user_id,
      info,
      base::Bind(&SupervisedUserService::OnSupervisedUserRegistered,
                 weak_ptr_factory_.GetWeakPtr(), callback, custodian_profile));

  // Fetch the custodian's profile information, to store the name.
  // TODO(pamg): If --google-profile-info (flag: switches::kGoogleProfileInfo)
  // is ever enabled, take the name from the ProfileAttributesStorage instead.
  CustodianProfileDownloaderService* profile_downloader_service =
      CustodianProfileDownloaderServiceFactory::GetForProfile(
          custodian_profile);
  profile_downloader_service->DownloadProfile(
      base::Bind(&SupervisedUserService::OnCustodianProfileDownloaded,
                 weak_ptr_factory_.GetWeakPtr()));
}
#endif  // !defined(OS_ANDROID)

void SupervisedUserService::AddNavigationBlockedCallback(
    const NavigationBlockedCallback& callback) {
  navigation_blocked_callbacks_.push_back(callback);
}

void SupervisedUserService::DidBlockNavigation(
    content::WebContents* web_contents) {
  for (const auto& callback : navigation_blocked_callbacks_)
    callback.Run(web_contents);
}

void SupervisedUserService::AddObserver(
    SupervisedUserServiceObserver* observer) {
  observer_list_.AddObserver(observer);
}

void SupervisedUserService::RemoveObserver(
    SupervisedUserServiceObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void SupervisedUserService::AddPermissionRequestCreator(
    std::unique_ptr<PermissionRequestCreator> creator) {
  permissions_creators_.push_back(creator.release());
}

void SupervisedUserService::SetSafeSearchURLReporter(
    std::unique_ptr<SafeSearchURLReporter> reporter) {
  url_reporter_ = std::move(reporter);
}

bool SupervisedUserService::IncludesSyncSessionsType() const {
  return includes_sync_sessions_type_;
}

SupervisedUserService::URLFilterContext::URLFilterContext()
    : ui_url_filter_(new SupervisedUserURLFilter),
      io_url_filter_(new SupervisedUserURLFilter) {}
SupervisedUserService::URLFilterContext::~URLFilterContext() {}

SupervisedUserURLFilter*
SupervisedUserService::URLFilterContext::ui_url_filter() const {
  return ui_url_filter_.get();
}

SupervisedUserURLFilter*
SupervisedUserService::URLFilterContext::io_url_filter() const {
  return io_url_filter_.get();
}

void SupervisedUserService::URLFilterContext::SetDefaultFilteringBehavior(
    SupervisedUserURLFilter::FilteringBehavior behavior) {
  ui_url_filter_->SetDefaultFilteringBehavior(behavior);
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::SetDefaultFilteringBehavior,
                 io_url_filter_.get(), behavior));
}

void SupervisedUserService::URLFilterContext::LoadWhitelists(
    const std::vector<scoped_refptr<SupervisedUserSiteList> >& site_lists) {
  ui_url_filter_->LoadWhitelists(site_lists);
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&SupervisedUserURLFilter::LoadWhitelists,
                                     io_url_filter_, site_lists));
}

void SupervisedUserService::URLFilterContext::SetBlacklist(
    const SupervisedUserBlacklist* blacklist) {
  ui_url_filter_->SetBlacklist(blacklist);
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::SetBlacklist,
                 io_url_filter_,
                 blacklist));
}

bool SupervisedUserService::URLFilterContext::HasBlacklist() const {
  return ui_url_filter_->HasBlacklist();
}

void SupervisedUserService::URLFilterContext::SetManualHosts(
    std::unique_ptr<std::map<std::string, bool>> host_map) {
  ui_url_filter_->SetManualHosts(host_map.get());
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::SetManualHosts,
                 io_url_filter_, base::Owned(host_map.release())));
}

void SupervisedUserService::URLFilterContext::SetManualURLs(
    std::unique_ptr<std::map<GURL, bool>> url_map) {
  ui_url_filter_->SetManualURLs(url_map.get());
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::SetManualURLs,
                 io_url_filter_, base::Owned(url_map.release())));
}

void SupervisedUserService::URLFilterContext::Clear() {
  ui_url_filter_->Clear();
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::Clear,
                 io_url_filter_));
}

void SupervisedUserService::URLFilterContext::InitAsyncURLChecker(
    const scoped_refptr<net::URLRequestContextGetter>& context) {
  ui_url_filter_->InitAsyncURLChecker(context.get());
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::InitAsyncURLChecker, io_url_filter_,
                 base::RetainedRef(context)));
}

bool SupervisedUserService::URLFilterContext::HasAsyncURLChecker() const {
  return ui_url_filter_->HasAsyncURLChecker();
}

void SupervisedUserService::URLFilterContext::ClearAsyncURLChecker() {
  ui_url_filter_->ClearAsyncURLChecker();
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&SupervisedUserURLFilter::ClearAsyncURLChecker,
                 io_url_filter_));
}

SupervisedUserService::SupervisedUserService(Profile* profile)
    : includes_sync_sessions_type_(true),
      profile_(profile),
      active_(false),
      delegate_(NULL),
      waiting_for_sync_initialization_(false),
      is_profile_active_(false),
      did_init_(false),
      did_shutdown_(false),
      blacklist_state_(BlacklistLoadState::NOT_LOADED),
#if defined(ENABLE_EXTENSIONS)
      registry_observer_(this),
#endif
      weak_ptr_factory_(this) {
  url_filter_context_.ui_url_filter()->AddObserver(this);
#if defined(ENABLE_EXTENSIONS)
  registry_observer_.Add(extensions::ExtensionRegistry::Get(profile));
#endif
}

void SupervisedUserService::SetActive(bool active) {
  if (active_ == active)
    return;
  active_ = active;

  if (!delegate_ || !delegate_->SetActive(active_)) {
    if (active_) {
#if !defined(OS_ANDROID)
      SupervisedUserPrefMappingServiceFactory::GetForBrowserContext(profile_)
          ->Init();

      base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
      if (command_line->HasSwitch(switches::kSupervisedUserSyncToken)) {
        InitSync(
            command_line->GetSwitchValueASCII(
                switches::kSupervisedUserSyncToken));
      }

      ProfileOAuth2TokenService* token_service =
          ProfileOAuth2TokenServiceFactory::GetForProfile(profile_);
      token_service->LoadCredentials(
          supervised_users::kSupervisedUserPseudoEmail);

      permissions_creators_.push_back(new PermissionRequestCreatorSync(
          GetSettingsService(),
          SupervisedUserSharedSettingsServiceFactory::GetForBrowserContext(
              profile_),
          ProfileSyncServiceFactory::GetForProfile(profile_),
          GetSupervisedUserName(),
          profile_->GetPrefs()->GetString(prefs::kSupervisedUserId)));

      SetupSync();
#else
      NOTREACHED();
#endif
    }
  }

  // Now activate/deactivate anything not handled by the delegate yet.

#if defined(ENABLE_THEMES)
  // Re-set the default theme to turn the SU theme on/off.
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile_);
  if (theme_service->UsingDefaultTheme() || theme_service->UsingSystemTheme())
    theme_service->UseDefaultTheme();
#endif

  ProfileSyncService* sync_service =
      ProfileSyncServiceFactory::GetForProfile(profile_);
  sync_service->SetEncryptEverythingAllowed(!active_);

  GetSettingsService()->SetActive(active_);

#if defined(ENABLE_EXTENSIONS)
  SetExtensionsActive();
#endif

  if (active_) {
    pref_change_registrar_.Add(
        prefs::kDefaultSupervisedUserFilteringBehavior,
        base::Bind(&SupervisedUserService::OnDefaultFilteringBehaviorChanged,
            base::Unretained(this)));
#if defined(ENABLE_EXTENSIONS)
    pref_change_registrar_.Add(
        prefs::kSupervisedUserApprovedExtensions,
        base::Bind(&SupervisedUserService::UpdateApprovedExtensions,
                   base::Unretained(this)));
#endif
    pref_change_registrar_.Add(prefs::kSupervisedUserSafeSites,
        base::Bind(&SupervisedUserService::OnSafeSitesSettingChanged,
                   base::Unretained(this)));
    pref_change_registrar_.Add(prefs::kSupervisedUserManualHosts,
        base::Bind(&SupervisedUserService::UpdateManualHosts,
                   base::Unretained(this)));
    pref_change_registrar_.Add(prefs::kSupervisedUserManualURLs,
        base::Bind(&SupervisedUserService::UpdateManualURLs,
                   base::Unretained(this)));
    for (const char* pref : kCustodianInfoPrefs) {
      pref_change_registrar_.Add(pref,
          base::Bind(&SupervisedUserService::OnCustodianInfoChanged,
                     base::Unretained(this)));
    }

    // Initialize the filter.
    OnDefaultFilteringBehaviorChanged();
    OnSafeSitesSettingChanged();
    whitelist_service_->Init();
    UpdateManualHosts();
    UpdateManualURLs();

#if defined(ENABLE_EXTENSIONS)
    UpdateApprovedExtensions();
#endif

#if !defined(OS_ANDROID)
    // TODO(bauerb): Get rid of the platform-specific #ifdef here.
    // http://crbug.com/313377
    BrowserList::AddObserver(this);
#endif
  } else {
    permissions_creators_.clear();
    url_reporter_.reset();

    pref_change_registrar_.Remove(
        prefs::kDefaultSupervisedUserFilteringBehavior);
#if defined(ENABLE_EXTENSIONS)
    pref_change_registrar_.Remove(prefs::kSupervisedUserApprovedExtensions);
#endif
    pref_change_registrar_.Remove(prefs::kSupervisedUserManualHosts);
    pref_change_registrar_.Remove(prefs::kSupervisedUserManualURLs);
    for (const char* pref : kCustodianInfoPrefs) {
      pref_change_registrar_.Remove(pref);
    }

    url_filter_context_.Clear();
    FOR_EACH_OBSERVER(
        SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());

#if !defined(OS_ANDROID)
    if (waiting_for_sync_initialization_)
      ProfileSyncServiceFactory::GetForProfile(profile_)->RemoveObserver(this);

    // TODO(bauerb): Get rid of the platform-specific #ifdef here.
    // http://crbug.com/313377
    BrowserList::RemoveObserver(this);
#endif
  }
}

#if !defined(OS_ANDROID)
void SupervisedUserService::OnCustodianProfileDownloaded(
    const base::string16& full_name) {
  profile_->GetPrefs()->SetString(prefs::kSupervisedUserCustodianName,
                                  base::UTF16ToUTF8(full_name));
}

void SupervisedUserService::OnSupervisedUserRegistered(
    const AuthErrorCallback& callback,
    Profile* custodian_profile,
    const GoogleServiceAuthError& auth_error,
    const std::string& token) {
  if (auth_error.state() == GoogleServiceAuthError::NONE) {
    InitSync(token);
    SigninManagerBase* signin =
        SigninManagerFactory::GetForProfile(custodian_profile);
    profile_->GetPrefs()->SetString(
        prefs::kSupervisedUserCustodianEmail,
        signin->GetAuthenticatedAccountInfo().email);

    // The supervised user profile is now ready for use.
    ProfileAttributesEntry* entry = nullptr;
    bool has_entry =
        g_browser_process->profile_manager()->GetProfileAttributesStorage().
            GetProfileAttributesWithPath(profile_->GetPath(), &entry);
    DCHECK(has_entry);
    entry->SetIsOmitted(false);
  } else {
    DCHECK_EQ(std::string(), token);
  }

  callback.Run(auth_error);
}
void SupervisedUserService::SetupSync() {
  StartSetupSync();
  FinishSetupSyncWhenReady();
}

void SupervisedUserService::StartSetupSync() {
  // Tell the sync service that setup is in progress so we don't start syncing
  // until we've finished configuration.
  sync_blocker_ = ProfileSyncServiceFactory::GetForProfile(profile_)
                      ->GetSetupInProgressHandle();
}

void SupervisedUserService::FinishSetupSyncWhenReady() {
  // If we're already waiting for the Sync backend, there's nothing to do here.
  if (waiting_for_sync_initialization_)
    return;

  // Continue in FinishSetupSync() once the Sync backend has been initialized.
  ProfileSyncService* service =
      ProfileSyncServiceFactory::GetForProfile(profile_);
  if (service->IsBackendInitialized()) {
    FinishSetupSync();
  } else {
    service->AddObserver(this);
    waiting_for_sync_initialization_ = true;
  }
}

void SupervisedUserService::FinishSetupSync() {
  ProfileSyncService* service =
      ProfileSyncServiceFactory::GetForProfile(profile_);
  DCHECK(service->IsBackendInitialized());

  // Sync nothing (except types which are set via GetPreferredDataTypes).
  bool sync_everything = false;
  syncer::ModelTypeSet synced_datatypes;
  service->OnUserChoseDatatypes(sync_everything, synced_datatypes);

  // Notify ProfileSyncService that we are done with configuration.
  sync_blocker_.reset();
  service->SetFirstSetupComplete();
}
#endif

bool SupervisedUserService::ProfileIsSupervised() const {
  return profile_->IsSupervised();
}

void SupervisedUserService::OnCustodianInfoChanged() {
  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnCustodianInfoChanged());
}

SupervisedUserSettingsService* SupervisedUserService::GetSettingsService() {
  return SupervisedUserSettingsServiceFactory::GetForProfile(profile_);
}

size_t SupervisedUserService::FindEnabledPermissionRequestCreator(
    size_t start) {
  for (size_t i = start; i < permissions_creators_.size(); ++i) {
    if (permissions_creators_[i]->IsEnabled())
      return i;
  }
  return permissions_creators_.size();
}

void SupervisedUserService::AddPermissionRequestInternal(
    const CreatePermissionRequestCallback& create_request,
    const SuccessCallback& callback,
    size_t index) {
  // Find a permission request creator that is enabled.
  size_t next_index = FindEnabledPermissionRequestCreator(index);
  if (next_index >= permissions_creators_.size()) {
    callback.Run(false);
    return;
  }

  create_request.Run(
      permissions_creators_[next_index],
      base::Bind(&SupervisedUserService::OnPermissionRequestIssued,
                 weak_ptr_factory_.GetWeakPtr(), create_request,
                 callback, next_index));
}

void SupervisedUserService::OnPermissionRequestIssued(
    const CreatePermissionRequestCallback& create_request,
    const SuccessCallback& callback,
    size_t index,
    bool success) {
  if (success) {
    callback.Run(true);
    return;
  }

  AddPermissionRequestInternal(create_request, callback, index + 1);
}

void SupervisedUserService::OnSupervisedUserIdChanged() {
  SetActive(ProfileIsSupervised());
}

void SupervisedUserService::OnDefaultFilteringBehaviorChanged() {
  int behavior_value = profile_->GetPrefs()->GetInteger(
      prefs::kDefaultSupervisedUserFilteringBehavior);
  SupervisedUserURLFilter::FilteringBehavior behavior =
      SupervisedUserURLFilter::BehaviorFromInt(behavior_value);
  url_filter_context_.SetDefaultFilteringBehavior(behavior);

  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());
}

void SupervisedUserService::OnSafeSitesSettingChanged() {
  bool use_blacklist = supervised_users::IsSafeSitesBlacklistEnabled(profile_);
  if (use_blacklist != url_filter_context_.HasBlacklist()) {
    if (use_blacklist && blacklist_state_ == BlacklistLoadState::NOT_LOADED) {
      LoadBlacklist(GetBlacklistPath(), GURL(kBlacklistURL));
    } else if (!use_blacklist ||
               blacklist_state_ == BlacklistLoadState::LOADED) {
      // Either the blacklist was turned off, or it was turned on but has
      // already been loaded previously. Just update the setting.
      UpdateBlacklist();
    }
    // Else: The blacklist was enabled, but the load is already in progress.
    // Do nothing - we'll check the setting again when the load finishes.
  }

  bool use_online_check =
      supervised_users::IsSafeSitesOnlineCheckEnabled(profile_);
  if (use_online_check != url_filter_context_.HasAsyncURLChecker()) {
    if (use_online_check)
      url_filter_context_.InitAsyncURLChecker(profile_->GetRequestContext());
    else
      url_filter_context_.ClearAsyncURLChecker();
  }
}

void SupervisedUserService::OnSiteListsChanged(
    const std::vector<scoped_refptr<SupervisedUserSiteList> >& site_lists) {
  whitelists_ = site_lists;
  url_filter_context_.LoadWhitelists(site_lists);
}

void SupervisedUserService::LoadBlacklist(const base::FilePath& path,
                                          const GURL& url) {
  DCHECK(blacklist_state_ == BlacklistLoadState::NOT_LOADED);
  blacklist_state_ = BlacklistLoadState::LOAD_STARTED;
  base::PostTaskAndReplyWithResult(
      BrowserThread::GetBlockingPool()->GetTaskRunnerWithShutdownBehavior(
          base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN).get(),
      FROM_HERE,
      base::Bind(&base::PathExists, path),
      base::Bind(&SupervisedUserService::OnBlacklistFileChecked,
                 weak_ptr_factory_.GetWeakPtr(), path, url));
}

void SupervisedUserService::OnBlacklistFileChecked(const base::FilePath& path,
                                                   const GURL& url,
                                                   bool file_exists) {
  DCHECK(blacklist_state_ == BlacklistLoadState::LOAD_STARTED);
  if (file_exists) {
    LoadBlacklistFromFile(path);
    return;
  }

  DCHECK(!blacklist_downloader_);
  blacklist_downloader_.reset(new FileDownloader(
      url,
      path,
      false,
      profile_->GetRequestContext(),
      base::Bind(&SupervisedUserService::OnBlacklistDownloadDone,
                 base::Unretained(this), path)));
}

void SupervisedUserService::LoadBlacklistFromFile(const base::FilePath& path) {
  DCHECK(blacklist_state_ == BlacklistLoadState::LOAD_STARTED);
  blacklist_.ReadFromFile(
      path,
      base::Bind(&SupervisedUserService::OnBlacklistLoaded,
                 base::Unretained(this)));
}

void SupervisedUserService::OnBlacklistDownloadDone(
    const base::FilePath& path,
    FileDownloader::Result result) {
  DCHECK(blacklist_state_ == BlacklistLoadState::LOAD_STARTED);
  if (FileDownloader::IsSuccess(result)) {
    LoadBlacklistFromFile(path);
  } else {
    LOG(WARNING) << "Blacklist download failed";
    // TODO(treib): Retry downloading after some time?
  }
  blacklist_downloader_.reset();
}

void SupervisedUserService::OnBlacklistLoaded() {
  DCHECK(blacklist_state_ == BlacklistLoadState::LOAD_STARTED);
  blacklist_state_ = BlacklistLoadState::LOADED;
  UpdateBlacklist();
}

void SupervisedUserService::UpdateBlacklist() {
  bool use_blacklist = supervised_users::IsSafeSitesBlacklistEnabled(profile_);
  url_filter_context_.SetBlacklist(use_blacklist ? &blacklist_ : nullptr);
  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());
}

void SupervisedUserService::UpdateManualHosts() {
  const base::DictionaryValue* dict =
      profile_->GetPrefs()->GetDictionary(prefs::kSupervisedUserManualHosts);
  std::unique_ptr<std::map<std::string, bool>> host_map(
      new std::map<std::string, bool>());
  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    bool allow = false;
    bool result = it.value().GetAsBoolean(&allow);
    DCHECK(result);
    (*host_map)[it.key()] = allow;
  }
  url_filter_context_.SetManualHosts(std::move(host_map));

  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());
}

void SupervisedUserService::UpdateManualURLs() {
  const base::DictionaryValue* dict =
      profile_->GetPrefs()->GetDictionary(prefs::kSupervisedUserManualURLs);
  std::unique_ptr<std::map<GURL, bool>> url_map(new std::map<GURL, bool>());
  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    bool allow = false;
    bool result = it.value().GetAsBoolean(&allow);
    DCHECK(result);
    (*url_map)[GURL(it.key())] = allow;
  }
  url_filter_context_.SetManualURLs(std::move(url_map));

  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());
}

std::string SupervisedUserService::GetSupervisedUserName() const {
#if defined(OS_CHROMEOS)
  // The active user can be NULL in unit tests.
  if (user_manager::UserManager::Get()->GetActiveUser()) {
    return UTF16ToUTF8(user_manager::UserManager::Get()->GetUserDisplayName(
        user_manager::UserManager::Get()->GetActiveUser()->GetAccountId()));
  }
  return std::string();
#else
  return profile_->GetPrefs()->GetString(prefs::kProfileName);
#endif
}

void SupervisedUserService::OnForceSessionSyncChanged() {
  includes_sync_sessions_type_ =
      profile_->GetPrefs()->GetBoolean(prefs::kForceSessionSync);
  ProfileSyncServiceFactory::GetForProfile(profile_)
      ->ReconfigureDatatypeManager();
}

void SupervisedUserService::Shutdown() {
  if (!did_init_)
    return;
  DCHECK(!did_shutdown_);
  did_shutdown_ = true;
  if (ProfileIsSupervised()) {
    content::RecordAction(UserMetricsAction("ManagedUsers_QuitBrowser"));
  }
  SetActive(false);
  sync_blocker_.reset();

  ProfileSyncService* sync_service =
      ProfileSyncServiceFactory::GetForProfile(profile_);

  // Can be null in tests.
  if (sync_service)
    sync_service->RemovePreferenceProvider(this);
}

#if defined(ENABLE_EXTENSIONS)
SupervisedUserService::ExtensionState SupervisedUserService::GetExtensionState(
    const Extension& extension) const {
  bool was_installed_by_default = extension.was_installed_by_default();
#if defined(OS_CHROMEOS)
  // On Chrome OS all external sources are controlled by us so it means that
  // they are "default". Method was_installed_by_default returns false because
  // extensions creation flags are ignored in case of default extensions with
  // update URL(the flags aren't passed to OnExternalExtensionUpdateUrlFound).
  // TODO(dpolukhin): remove this Chrome OS specific code as soon as creation
  // flags are not ignored.
  was_installed_by_default =
      extensions::Manifest::IsExternalLocation(extension.location());
#endif
  // Note: Component extensions are protected from modification/uninstallation
  // anyway, so there's no need to enforce them again for supervised users.
  // Also, leave policy-installed extensions alone - they have their own
  // management; in particular we don't want to override the force-install list.
  if (extensions::Manifest::IsComponentLocation(extension.location()) ||
      extensions::Manifest::IsPolicyLocation(extension.location()) ||
      extension.is_theme() || extension.from_bookmark() ||
      extension.is_shared_module() || was_installed_by_default) {
    return ExtensionState::ALLOWED;
  }

  if (extensions::util::WasInstalledByCustodian(extension.id(), profile_))
    return ExtensionState::FORCED;

  if (!base::FeatureList::IsEnabled(
          supervised_users::kSupervisedUserInitiatedExtensionInstall)) {
    return ExtensionState::BLOCKED;
  }

  auto extension_it = approved_extensions_map_.find(extension.id());
  // If the installed version is approved, then the extension is allowed,
  // otherwise, it requires approval.
  if (extension_it != approved_extensions_map_.end() &&
      extension_it->second == *extension.version()) {
    return ExtensionState::ALLOWED;
  }
  return ExtensionState::REQUIRE_APPROVAL;
}

std::string SupervisedUserService::GetDebugPolicyProviderName() const {
  // Save the string space in official builds.
#ifdef NDEBUG
  NOTREACHED();
  return std::string();
#else
  return "Supervised User Service";
#endif
}

bool SupervisedUserService::UserMayLoad(const Extension* extension,
                                        base::string16* error) const {
  DCHECK(ProfileIsSupervised());
  ExtensionState result = GetExtensionState(*extension);
  bool may_load = result != ExtensionState::BLOCKED;
  if (!may_load && error)
    *error = GetExtensionsLockedMessage();
  return may_load;
}

bool SupervisedUserService::UserMayModifySettings(const Extension* extension,
                                                  base::string16* error) const {
  DCHECK(ProfileIsSupervised());
  ExtensionState result = GetExtensionState(*extension);
  // While the following check allows the supervised user to modify the settings
  // and enable or disable the extension, MustRemainDisabled properly takes care
  // of keeping an extension disabled when required.
  // For custodian-installed extensions, the state is always FORCED, even if
  // it's waiting for an update approval.
  bool may_modify = result != ExtensionState::FORCED;
  if (!may_modify && error)
    *error = GetExtensionsLockedMessage();
  return may_modify;
}

// Note: Having MustRemainInstalled always say "true" for custodian-installed
// extensions does NOT prevent remote uninstalls (which is a bit unexpected, but
// exactly what we want).
bool SupervisedUserService::MustRemainInstalled(const Extension* extension,
                                                base::string16* error) const {
  DCHECK(ProfileIsSupervised());
  ExtensionState result = GetExtensionState(*extension);
  bool may_not_uninstall = result == ExtensionState::FORCED;
  if (may_not_uninstall && error)
    *error = GetExtensionsLockedMessage();
  return may_not_uninstall;
}

bool SupervisedUserService::MustRemainDisabled(const Extension* extension,
                                               Extension::DisableReason* reason,
                                               base::string16* error) const {
  DCHECK(ProfileIsSupervised());
  ExtensionState state = GetExtensionState(*extension);
  // Only extensions that require approval should be disabled.
  // Blocked extensions should be not loaded at all, and are taken care of
  // at UserMayLoad.
  bool must_remain_disabled = state == ExtensionState::REQUIRE_APPROVAL;

  if (must_remain_disabled) {
    if (error)
      *error = l10n_util::GetStringUTF16(IDS_EXTENSIONS_LOCKED_SUPERVISED_USER);
    // If the extension must remain disabled due to permission increase,
    // then the update request has been already sent at update time.
    // We do nothing and we don't add an extra disable reason.
    ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(profile_);
    if (extension_prefs->HasDisableReason(
            extension->id(), Extension::DISABLE_PERMISSIONS_INCREASE)) {
      if (reason)
        *reason = Extension::DISABLE_PERMISSIONS_INCREASE;
      return true;
    }
    if (reason)
      *reason = Extension::DISABLE_CUSTODIAN_APPROVAL_REQUIRED;
    if (base::FeatureList::IsEnabled(
            supervised_users::kSupervisedUserInitiatedExtensionInstall)) {
      // If the Extension isn't pending a custodian approval already, send
      // an approval request.
      if (!extension_prefs->HasDisableReason(
              extension->id(),
              Extension::DISABLE_CUSTODIAN_APPROVAL_REQUIRED)) {
        // MustRemainDisabled is a const method and hence cannot call
        // AddExtensionInstallRequest directly.
        SupervisedUserService* supervised_user_service =
            SupervisedUserServiceFactory::GetForProfile(profile_);
        supervised_user_service->AddExtensionInstallRequest(
            extension->id(), *extension->version());
      }
    }
  }
  return must_remain_disabled;
}

void SupervisedUserService::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    bool is_update) {
  // This callback method is responsible for updating extension state and
  // approved_extensions_map_ upon extension updates.
  if (!is_update)
    return;

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(profile_);
  const std::string& id = extension->id();
  const base::Version& version = *extension->version();

  // If an already approved extension is updated without requiring
  // new permissions, we update the approved_version.
  if (!extension_prefs->HasDisableReason(
          id, Extension::DISABLE_PERMISSIONS_INCREASE) &&
      approved_extensions_map_.count(id) > 0 &&
      approved_extensions_map_[id] < version) {
    approved_extensions_map_[id] = version;

    std::string key = SupervisedUserSettingsService::MakeSplitSettingKey(
        supervised_users::kApprovedExtensions, id);
    std::unique_ptr<base::Value> version_value(
        new base::StringValue(version.GetString()));
    GetSettingsService()->UpdateSetting(key, std::move(version_value));
  }
  // Upon extension update, the approved version may (or may not) match the
  // installed one. Therefore, a change in extension state might be required.
  ChangeExtensionStateIfNecessary(id);
}

void SupervisedUserService::UpdateApprovedExtensions() {
  const base::DictionaryValue* dict = profile_->GetPrefs()->GetDictionary(
      prefs::kSupervisedUserApprovedExtensions);
  // Keep track of currently approved extensions. We may need to disable them if
  // they are not in the approved map anymore.
  std::set<std::string> extensions_to_be_checked;
  for (const auto& extension : approved_extensions_map_)
    extensions_to_be_checked.insert(extension.first);

  approved_extensions_map_.clear();

  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    std::string version_str;
    bool result = it.value().GetAsString(&version_str);
    DCHECK(result);
    base::Version version(version_str);
    if (version.IsValid()) {
      approved_extensions_map_[it.key()] = version;
      extensions_to_be_checked.insert(it.key());
    } else {
      LOG(WARNING) << "Invalid version number " << version_str;
    }
  }

  for (const auto& extension_id : extensions_to_be_checked) {
    ChangeExtensionStateIfNecessary(extension_id);
  }
}

void SupervisedUserService::ChangeExtensionStateIfNecessary(
    const std::string& extension_id) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile_);
  const Extension* extension = registry->GetInstalledExtension(extension_id);
  // If the extension is not installed (yet), do nothing.
  // Things will be handled after installation.
  if (!extension)
    return;

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(profile_);
  ExtensionService* service =
      ExtensionSystem::Get(profile_)->extension_service();

  ExtensionState state = GetExtensionState(*extension);
  switch (state) {
    // BLOCKED/FORCED extensions should be already disabled/enabled
    // and we don't need to change their state here.
    case ExtensionState::BLOCKED:
    case ExtensionState::FORCED:
      break;
    case ExtensionState::REQUIRE_APPROVAL:
      service->DisableExtension(extension_id,
                                Extension::DISABLE_CUSTODIAN_APPROVAL_REQUIRED);
      break;
    case ExtensionState::ALLOWED:
      extension_prefs->RemoveDisableReason(
          extension_id, Extension::DISABLE_CUSTODIAN_APPROVAL_REQUIRED);
      extension_prefs->RemoveDisableReason(
          extension_id, Extension::DISABLE_PERMISSIONS_INCREASE);
      // If not disabled for other reasons, enable it.
      if (extension_prefs->GetDisableReasons(extension_id) ==
          Extension::DISABLE_NONE) {
        service->EnableExtension(extension_id);
      }
      break;
  }
}

void SupervisedUserService::SetExtensionsActive() {
  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile_);
  extensions::ManagementPolicy* management_policy =
      extension_system->management_policy();

  if (management_policy) {
    if (active_)
      management_policy->RegisterProvider(this);
    else
      management_policy->UnregisterProvider(this);

    // Re-check the policy to make sure any new settings get applied.
    extension_system->extension_service()->CheckManagementPolicy();
  }
}
#endif  // defined(ENABLE_EXTENSIONS)

syncer::ModelTypeSet SupervisedUserService::GetPreferredDataTypes() const {
  if (!ProfileIsSupervised())
    return syncer::ModelTypeSet();

  syncer::ModelTypeSet result;
  if (IncludesSyncSessionsType())
    result.Put(syncer::SESSIONS);
  result.Put(syncer::EXTENSIONS);
  result.Put(syncer::EXTENSION_SETTINGS);
  result.Put(syncer::APPS);
  result.Put(syncer::APP_SETTINGS);
  result.Put(syncer::APP_NOTIFICATIONS);
  result.Put(syncer::APP_LIST);
  return result;
}

#if !defined(OS_ANDROID)
void SupervisedUserService::OnStateChanged() {
  ProfileSyncService* service =
      ProfileSyncServiceFactory::GetForProfile(profile_);
  if (waiting_for_sync_initialization_ && service->IsBackendInitialized()) {
    waiting_for_sync_initialization_ = false;
    service->RemoveObserver(this);
    FinishSetupSync();
    return;
  }

  DLOG_IF(ERROR, service->GetAuthError().state() ==
                     GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS)
      << "Credentials rejected";
}

void SupervisedUserService::OnBrowserSetLastActive(Browser* browser) {
  bool profile_became_active = profile_->IsSameProfile(browser->profile());
  if (!is_profile_active_ && profile_became_active)
    content::RecordAction(UserMetricsAction("ManagedUsers_OpenProfile"));
  else if (is_profile_active_ && !profile_became_active)
    content::RecordAction(UserMetricsAction("ManagedUsers_SwitchProfile"));

  is_profile_active_ = profile_became_active;
}
#endif  // !defined(OS_ANDROID)

void SupervisedUserService::OnSiteListUpdated() {
  FOR_EACH_OBSERVER(
      SupervisedUserServiceObserver, observer_list_, OnURLFilterChanged());
}
