// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/content_setting_bubble_contents.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/media/media_capture_devices_dispatcher.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/plugins/plugin_metadata.h"
#include "chrome/browser/ui/content_settings/content_setting_bubble_model.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/web_contents.h"
#include "grit/components_strings.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/native_cursor.h"

namespace {

// If we don't clamp the maximum width, then very long URLs and titles can make
// the bubble arbitrarily wide.
const int kMaxContentsWidth = 500;

// When we have multiline labels, we should set a minimum width lest we get very
// narrow bubbles with lots of line-wrapping.
const int kMinMultiLineContentsWidth = 250;

}  // namespace

using content::PluginService;
using content::WebContents;

// ContentSettingBubbleContents::MediaComboboxModel ----------------------------

ContentSettingBubbleContents::MediaComboboxModel::MediaComboboxModel(
    content::MediaStreamType type)
    : type_(type) {
  DCHECK(type_ == content::MEDIA_DEVICE_AUDIO_CAPTURE ||
         type_ == content::MEDIA_DEVICE_VIDEO_CAPTURE);
}

ContentSettingBubbleContents::MediaComboboxModel::~MediaComboboxModel() {}

const content::MediaStreamDevices&
ContentSettingBubbleContents::MediaComboboxModel::GetDevices() const {
  MediaCaptureDevicesDispatcher* dispatcher =
      MediaCaptureDevicesDispatcher::GetInstance();
  return type_ == content::MEDIA_DEVICE_AUDIO_CAPTURE
             ? dispatcher->GetAudioCaptureDevices()
             : dispatcher->GetVideoCaptureDevices();
}

int ContentSettingBubbleContents::MediaComboboxModel::GetDeviceIndex(
    const content::MediaStreamDevice& device) const {
  const auto& devices = GetDevices();
  for (size_t i = 0; i < devices.size(); ++i) {
    if (device.id == devices[i].id)
      return i;
  }
  NOTREACHED();
  return 0;
}

int ContentSettingBubbleContents::MediaComboboxModel::GetItemCount() const {
  return std::max(1, static_cast<int>(GetDevices().size()));
}

base::string16 ContentSettingBubbleContents::MediaComboboxModel::GetItemAt(
    int index) {
  return GetDevices().empty()
             ? l10n_util::GetStringUTF16(IDS_MEDIA_MENU_NO_DEVICE_TITLE)
             : base::UTF8ToUTF16(GetDevices()[index].name);
}

// ContentSettingBubbleContents::Favicon --------------------------------------

class ContentSettingBubbleContents::Favicon : public views::ImageView {
 public:
  Favicon(const gfx::Image& image,
          ContentSettingBubbleContents* parent,
          views::Link* link);
  ~Favicon() override;

 private:
  // views::View overrides:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  gfx::NativeCursor GetCursor(const ui::MouseEvent& event) override;

  ContentSettingBubbleContents* parent_;
  views::Link* link_;
};

ContentSettingBubbleContents::Favicon::Favicon(
    const gfx::Image& image,
    ContentSettingBubbleContents* parent,
    views::Link* link)
    : parent_(parent),
      link_(link) {
  SetImage(image.AsImageSkia());
}

ContentSettingBubbleContents::Favicon::~Favicon() {
}

bool ContentSettingBubbleContents::Favicon::OnMousePressed(
    const ui::MouseEvent& event) {
  return event.IsLeftMouseButton() || event.IsMiddleMouseButton();
}

void ContentSettingBubbleContents::Favicon::OnMouseReleased(
    const ui::MouseEvent& event) {
  if ((event.IsLeftMouseButton() || event.IsMiddleMouseButton()) &&
     HitTestPoint(event.location())) {
    parent_->LinkClicked(link_, event.flags());
  }
}

gfx::NativeCursor ContentSettingBubbleContents::Favicon::GetCursor(
    const ui::MouseEvent& event) {
  return views::GetNativeHandCursor();
}


// ContentSettingBubbleContents -----------------------------------------------

ContentSettingBubbleContents::ContentSettingBubbleContents(
    ContentSettingBubbleModel* content_setting_bubble_model,
    content::WebContents* web_contents,
    views::View* anchor_view,
    views::BubbleBorder::Arrow arrow)
    : content::WebContentsObserver(web_contents),
      BubbleDialogDelegateView(anchor_view, arrow),
      content_setting_bubble_model_(content_setting_bubble_model),
      custom_link_(NULL),
      manage_link_(NULL),
      learn_more_link_(NULL) {
  // Compensate for built-in vertical padding in the anchor view's image.
  set_anchor_view_insets(gfx::Insets(
      GetLayoutConstant(LOCATION_BAR_BUBBLE_ANCHOR_VERTICAL_INSET), 0));
}

ContentSettingBubbleContents::~ContentSettingBubbleContents() {
  // Must remove the children here so the comboboxes get destroyed before
  // their associated models.
  RemoveAllChildViews(true);
}

gfx::Size ContentSettingBubbleContents::GetPreferredSize() const {
  gfx::Size preferred_size(views::View::GetPreferredSize());
  int preferred_width =
      (!content_setting_bubble_model_->bubble_content().domain_lists.empty() &&
       (kMinMultiLineContentsWidth > preferred_size.width()))
          ? kMinMultiLineContentsWidth
          : preferred_size.width();
  preferred_size.set_width(std::min(preferred_width, kMaxContentsWidth));
  return preferred_size;
}

void ContentSettingBubbleContents::Init() {
  using views::GridLayout;

  GridLayout* layout = new views::GridLayout(this);
  SetLayoutManager(layout);

  const int kSingleColumnSetId = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(kSingleColumnSetId);
  column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, views::kRelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);

  const ContentSettingBubbleModel::BubbleContent& bubble_content =
      content_setting_bubble_model_->bubble_content();
  bool bubble_content_empty = true;

  if (!bubble_content.title.empty()) {
    views::Label* title_label = new views::Label(base::UTF8ToUTF16(
        bubble_content.title));
    title_label->SetMultiLine(true);
    title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->StartRow(0, kSingleColumnSetId);
    layout->AddView(title_label);
    bubble_content_empty = false;
  }

  if (!bubble_content.learn_more_link.empty()) {
    learn_more_link_ =
        new views::Link(base::UTF8ToUTF16(bubble_content.learn_more_link));
    learn_more_link_->set_listener(this);
    learn_more_link_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(learn_more_link_);
    bubble_content_empty = false;
  }

  // Layout for the item list (blocked plugins and popups).
  if (!bubble_content.list_items.empty()) {
    const int kItemListColumnSetId = 2;
    views::ColumnSet* item_list_column_set =
        layout->AddColumnSet(kItemListColumnSetId);
    item_list_column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 0,
                                    GridLayout::USE_PREF, 0, 0);
    item_list_column_set->AddPaddingColumn(
        0, views::kRelatedControlHorizontalSpacing);
    item_list_column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                                    GridLayout::USE_PREF, 0, 0);

    int row = 0;
    for (const ContentSettingBubbleModel::ListItem& list_item :
         bubble_content.list_items) {
      if (!bubble_content_empty)
        layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
      layout->StartRow(0, kItemListColumnSetId);
      if (list_item.has_link) {
        views::Link* link = new views::Link(base::UTF8ToUTF16(list_item.title));
        link->set_listener(this);
        link->SetElideBehavior(gfx::ELIDE_MIDDLE);
        list_item_links_[link] = row;
        layout->AddView(new Favicon(list_item.image, this, link));
        layout->AddView(link);
      } else {
        views::ImageView* icon = new views::ImageView();
        icon->SetImage(list_item.image.AsImageSkia());
        layout->AddView(icon);
        layout->AddView(new views::Label(base::UTF8ToUTF16(list_item.title)));
      }
      row++;
      bubble_content_empty = false;
    }
  }

  const int indented_kSingleColumnSetId = 3;
  // Insert a column set with greater indent.
  views::ColumnSet* indented_single_column_set =
      layout->AddColumnSet(indented_kSingleColumnSetId);
  indented_single_column_set->AddPaddingColumn(0, views::kCheckboxIndent);
  indented_single_column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL,
                                        1, GridLayout::USE_PREF, 0, 0);

  const ContentSettingBubbleModel::RadioGroup& radio_group =
      bubble_content.radio_group;
  if (!radio_group.radio_items.empty()) {
    if (!bubble_content_empty)
      layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
    for (ContentSettingBubbleModel::RadioItems::const_iterator i(
         radio_group.radio_items.begin());
         i != radio_group.radio_items.end(); ++i) {
      views::RadioButton* radio =
          new views::RadioButton(base::UTF8ToUTF16(*i), 0);
      radio->SetEnabled(bubble_content.radio_group_enabled);
      radio->set_listener(this);
      radio_group_.push_back(radio);
      layout->StartRow(0, indented_kSingleColumnSetId);
      layout->AddView(radio);
      bubble_content_empty = false;
    }
    DCHECK(!radio_group_.empty());
    // Now that the buttons have been added to the view hierarchy, it's safe
    // to call SetChecked() on them.
    radio_group_[radio_group.default_item]->SetChecked(true);
  }

  // Layout code for the media device menus.
  if (content_setting_bubble_model_->AsMediaStreamBubbleModel()) {
    const int kMediaMenuColumnSetId = 4;
    views::ColumnSet* menu_column_set =
        layout->AddColumnSet(kMediaMenuColumnSetId);
    menu_column_set->AddPaddingColumn(0, views::kCheckboxIndent);
    menu_column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 0,
                               GridLayout::USE_PREF, 0, 0);
    menu_column_set->AddPaddingColumn(
        0, views::kRelatedControlHorizontalSpacing);
    menu_column_set->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                               GridLayout::USE_PREF, 0, 0);

    for (ContentSettingBubbleModel::MediaMenuMap::const_iterator i(
         bubble_content.media_menus.begin());
         i != bubble_content.media_menus.end(); ++i) {
      if (!bubble_content_empty)
        layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
      layout->StartRow(0, kMediaMenuColumnSetId);

      views::Label* label =
          new views::Label(base::UTF8ToUTF16(i->second.label));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      layout->AddView(label);

      combobox_models_.emplace_back(i->first);
      MediaComboboxModel* model = &combobox_models_.back();
      views::Combobox* combobox = new views::Combobox(model);
      // Disable the device selection when the website is managing the devices
      // itself or if there are no devices present.
      combobox->SetEnabled(
          !(i->second.disabled || model->GetDevices().empty()));
      combobox->set_listener(this);
      combobox->SetSelectedIndex(
          model->GetDevices().empty()
              ? 0
              : model->GetDeviceIndex(i->second.selected_device));
      layout->AddView(combobox);

      bubble_content_empty = false;
    }
  }

  const gfx::FontList& domain_font =
      ui::ResourceBundle::GetSharedInstance().GetFontList(
          ui::ResourceBundle::BoldFont);
  for (std::vector<ContentSettingBubbleModel::DomainList>::const_iterator i(
           bubble_content.domain_lists.begin());
       i != bubble_content.domain_lists.end(); ++i) {
    layout->StartRow(0, kSingleColumnSetId);
    views::Label* section_title = new views::Label(base::UTF8ToUTF16(i->title));
    section_title->SetMultiLine(true);
    section_title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->AddView(section_title, 1, 1, GridLayout::FILL, GridLayout::LEADING);
    for (std::set<std::string>::const_iterator j = i->hosts.begin();
         j != i->hosts.end(); ++j) {
      layout->StartRow(0, indented_kSingleColumnSetId);
      layout->AddView(new views::Label(base::UTF8ToUTF16(*j), domain_font));
    }
    bubble_content_empty = false;
  }

  if (!bubble_content.custom_link.empty()) {
    custom_link_ =
        new views::Link(base::UTF8ToUTF16(bubble_content.custom_link));
    custom_link_->SetEnabled(bubble_content.custom_link_enabled);
    custom_link_->set_listener(this);
    if (!bubble_content_empty)
      layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
    layout->StartRow(0, kSingleColumnSetId);
    layout->AddView(custom_link_);
    bubble_content_empty = false;
  }

  if (!bubble_content_empty) {
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
    layout->StartRow(0, kSingleColumnSetId);
    layout->AddView(new views::Separator(views::Separator::HORIZONTAL), 1, 1,
                    GridLayout::FILL, GridLayout::FILL);
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
  }
}

views::View* ContentSettingBubbleContents::CreateExtraView() {
  manage_link_ = new views::Link(base::UTF8ToUTF16(
      content_setting_bubble_model_->bubble_content().manage_link));
  manage_link_->set_listener(this);
  return manage_link_;
}

bool ContentSettingBubbleContents::Accept() {
  content_setting_bubble_model_->OnDoneClicked();
  return true;
}

bool ContentSettingBubbleContents::Close() {
  return true;
}

int ContentSettingBubbleContents::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_OK;
}

base::string16 ContentSettingBubbleContents::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return l10n_util::GetStringUTF16(IDS_DONE);
}

void ContentSettingBubbleContents::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  // Content settings are based on the main frame, so if it switches then
  // close up shop.
  GetWidget()->Close();
}

void ContentSettingBubbleContents::ButtonPressed(views::Button* sender,
                                                 const ui::Event& event) {
  RadioGroup::const_iterator i(
      std::find(radio_group_.begin(), radio_group_.end(), sender));
  DCHECK(i != radio_group_.end());
  content_setting_bubble_model_->OnRadioClicked(i - radio_group_.begin());
}

void ContentSettingBubbleContents::LinkClicked(views::Link* source,
                                               int event_flags) {
  if (source == learn_more_link_) {
    content_setting_bubble_model_->OnLearnMoreLinkClicked();
    GetWidget()->Close();
    return;
  }
  if (source == custom_link_) {
    content_setting_bubble_model_->OnCustomLinkClicked();
    GetWidget()->Close();
    return;
  }
  if (source == manage_link_) {
    GetWidget()->Close();
    content_setting_bubble_model_->OnManageLinkClicked();
    // CAREFUL: Showing the settings window activates it, which deactivates the
    // info bubble, which causes it to close, which deletes us.
    return;
  }

  ListItemLinks::const_iterator i(list_item_links_.find(source));
  DCHECK(i != list_item_links_.end());
  content_setting_bubble_model_->OnListItemClicked(i->second);
}

void ContentSettingBubbleContents::OnPerformAction(views::Combobox* combobox) {
  MediaComboboxModel* model =
      static_cast<MediaComboboxModel*>(combobox->model());
  content_setting_bubble_model_->OnMediaMenuClicked(
      model->type(), model->GetDevices()[combobox->selected_index()].id);
}
