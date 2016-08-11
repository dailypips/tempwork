// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_
#define CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/arc/arc_process.h"
#include "chrome/browser/memory/tab_manager.h"
#include "chrome/browser/memory/tab_stats.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/common/process.mojom.h"
#include "components/arc/instance_holder.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/wm/public/activation_change_observer.h"

namespace memory {

// Possible types of Apps/Tabs processes. From most important to least
// important.
enum class ProcessType {
  // There can be only one process having process type "FOCUSED_APP"
  // or "FOCUSED_TAB" in the system at any give time (i.e., The focused window
  // could be either a chrome window or an Android app. But not both.
  FOCUSED_APP = 1,
  FOCUSED_TAB = FOCUSED_APP,

  VISIBLE_APP = 2,
  BACKGROUND_TAB = 3,
  BACKGROUND_APP = 4,
  UNKNOWN_TYPE = 5,
};

// The Chrome OS TabManagerDelegate is responsible for keeping the
// renderers' scores up to date in /proc/<pid>/oom_score_adj.
//
// Note that AdjustOomPriorities will be called on the UI thread by
// TabManager, but the actual work will take place on the file thread
// (see implementation of AdjustOomPriorities).
class TabManagerDelegate
    : public arc::InstanceHolder<arc::mojom::ProcessInstance>::Observer,
      public aura::client::ActivationChangeObserver,
      public content::NotificationObserver,
      public chrome::BrowserListObserver {
 public:
  class MemoryStat;

  explicit TabManagerDelegate(const base::WeakPtr<TabManager>& tab_manager);

  TabManagerDelegate(const base::WeakPtr<TabManager>& tab_manager,
                     TabManagerDelegate::MemoryStat* mem_stat);

  ~TabManagerDelegate() override;

  void OnBrowserSetLastActive(Browser* browser) override;

  // InstanceHolder<arc::mojom::ProcessInstance>::Observer overrides.
  void OnInstanceReady() override;
  void OnInstanceClosed() override;

  // aura::ActivationChangeObserver overrides.
  void OnWindowActivated(
      aura::client::ActivationChangeObserver::ActivationReason reason,
      aura::Window* gained_active,
      aura::Window* lost_active) override;

  // Kills a process on memory pressure.
  void LowMemoryKill(const TabStatsList& tab_stats);

  // Returns oom_score_adj of a process if the score is cached by |this|.
  // If couldn't find the score in the cache, returns -1001 since the valid
  // range of oom_score_adj is [-1000, 1000].
  int GetCachedOomScore(base::ProcessHandle process_handle);

  // Called when the timer fires, sets oom_adjust_score for all renderers.
  void AdjustOomPriorities(const TabStatsList& tab_list);

 protected:
  // Sets oom_score_adj for a list of tabs.
  // This is a delegator to to SetOomScoreAdjForTabsOnFileThread(),
  // also as a seam for unit test.
  virtual void SetOomScoreAdjForTabs(
      const std::vector<std::pair<base::ProcessHandle, int>>& entries);

  // Kills an Arc process. Returns true if the kill request is successfully sent
  // to Android. Virtual for unit testing.
  virtual bool KillArcProcess(const int nspid);

  // Kills a tab. Returns true if the tab is killed successfully.
  // Virtual for unit testing.
  virtual bool KillTab(int64_t tab_id);

 private:
  FRIEND_TEST_ALL_PREFIXES(TabManagerDelegateTest, CandidatesSorted);
  FRIEND_TEST_ALL_PREFIXES(TabManagerDelegateTest, KillMultipleProcesses);
  FRIEND_TEST_ALL_PREFIXES(TabManagerDelegateTest, SetOomScoreAdj);

  class Candidate;
  class FocusedProcess;
  class UmaReporter;

  friend std::ostream& operator<<(std::ostream& out,
                                  const Candidate& candidate);

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Pair to hold child process host id and ProcessHandle.
  typedef std::pair<int, base::ProcessHandle> ProcessInfo;

  // Cache OOM scores in memory.
  typedef base::hash_map<base::ProcessHandle, int> ProcessScoreMap;

  // Get the list of candidates to kill, sorted by descending importance.
  static std::vector<Candidate> GetSortedCandidates(
      const TabStatsList& tab_list,
      const std::vector<arc::ArcProcess>& arc_processes);

  // Posts AdjustFocusedTabScore task to the file thread.
  void OnFocusTabScoreAdjustmentTimeout();

  // Kills a process after getting all info of tabs and apps.
  void LowMemoryKillImpl(const TabStatsList& tab_list,
                         const std::vector<arc::ArcProcess>& arc_processes);

  // Public interface to adjust OOM scores.
  void AdjustOomPriorities(const TabStatsList& tab_list,
                           const std::vector<arc::ArcProcess>& arc_processes);

  // Sets the score of the focused tab to the least value.
  void AdjustFocusedTabScoreOnFileThread();

  // Sets a newly focused tab the highest priority process if it wasn't.
  void AdjustFocusedTabScore(base::ProcessHandle pid);

  // Called by AdjustOomPriorities. Runs on the main thread.
  void AdjustOomPrioritiesImpl(
      const TabStatsList& tab_list,
      const std::vector<arc::ArcProcess>& arc_processes);

  // Sets oom_score_adj of an ARC app.
  void SetOomScoreAdjForApp(int nspid, int score);

  // Sets oom_score_adj for a list of tabs on the file thread.
  void SetOomScoreAdjForTabsOnFileThread(
      const std::vector<std::pair<base::ProcessHandle, int>>& entries);

  // Sets OOM score for processes in the range [|rbegin|, |rend|) to integers
  // distributed evenly in [|range_begin|, |range_end|).
  // The new score is set in |new_map|.
  void DistributeOomScoreInRange(
      std::vector<TabManagerDelegate::Candidate>::const_iterator begin,
      std::vector<TabManagerDelegate::Candidate>::const_iterator end,
      int range_begin,
      int range_end,
      ProcessScoreMap* new_map);

  // Initiates an oom priority adjustment.
  void ScheduleEarlyOomPrioritiesAdjustment();

  // Holds a reference to the owning TabManager.
  const base::WeakPtr<TabManager> tab_manager_;

  // Registrar to receive renderer notifications.
  content::NotificationRegistrar registrar_;

  // Timer to guarantee that the tab or app is focused for a certain amount of
  // time.
  base::OneShotTimer focus_process_score_adjust_timer_;
  // Holds the info of the newly focused tab or app. Its OOM score would be
  // adjusted when |focus_process_score_adjust_timer_| is expired.
  std::unique_ptr<FocusedProcess> focused_process_;

  // This lock is for |oom_score_map_|.
  base::Lock oom_score_lock_;
  // Map maintaining the process handle - oom_score mapping. Behind
  // |oom_score_lock_|.
  ProcessScoreMap oom_score_map_;

  // Util for getting system memory satatus.
  std::unique_ptr<TabManagerDelegate::MemoryStat> mem_stat_;

  // Holds a weak pointer to arc::mojom::ProcessInstance.
  arc::mojom::ProcessInstance* arc_process_instance_;
  // Current ProcessInstance version.
  int arc_process_instance_version_;

  // Reports UMA histograms.
  std::unique_ptr<UmaReporter> uma_;

  // Weak pointer factory used for posting tasks to other threads.
  base::WeakPtrFactory<TabManagerDelegate> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(TabManagerDelegate);
};

// On ARC enabled machines, either a tab or an app could be a possible
// victim of low memory kill process. This is a helper class which holds a
// pointer to an app or a tab (but not both) to facilitate prioritizing the
// victims.
class TabManagerDelegate::Candidate {
 public:
  explicit Candidate(const TabStats* tab)
      : tab_(tab), app_(nullptr), process_type_(GetProcessTypeInternal()) {
    DCHECK(tab_);
  }
  explicit Candidate(const arc::ArcProcess* app)
      : tab_(nullptr), app_(app), process_type_(GetProcessTypeInternal()) {
    DCHECK(app_);
  }

  // Move-only class.
  Candidate(Candidate&&) = default;
  Candidate& operator=(Candidate&& other);

  // Higher priority first.
  bool operator<(const Candidate& rhs) const;

  const TabStats* tab() const { return tab_; }
  const arc::ArcProcess* app() const { return app_; }
  ProcessType process_type() const { return process_type_; }

 private:
  // Derive process type for this candidate. Used to initialize |process_type_|.
  ProcessType GetProcessTypeInternal() const;

  const TabStats* tab_;
  const arc::ArcProcess* app_;
  ProcessType process_type_;
  DISALLOW_COPY_AND_ASSIGN(Candidate);
};

// A thin wrapper over library process_metric.h to get memory status so unit
// test get a chance to mock out.
class TabManagerDelegate::MemoryStat {
 public:
  MemoryStat() {}
  virtual ~MemoryStat() {}

  // Returns target size of memory to free given current memory pressure and
  // pre-configured low memory margin.
  virtual int TargetMemoryToFreeKB();

  // Returns estimated memory to be freed if the process |pid| is killed.
  virtual int EstimatedMemoryFreedKB(base::ProcessHandle pid);

 private:
  // Returns the low memory margin system config. Low memory condition is
  // reported if available memory is under the number.
  static int LowMemoryMarginKB();

  // Reads in an integer.
  static int ReadIntFromFile(const char* file_name, int default_val);
};

}  // namespace memory

#endif  // CHROME_BROWSER_MEMORY_TAB_MANAGER_DELEGATE_CHROMEOS_H_
