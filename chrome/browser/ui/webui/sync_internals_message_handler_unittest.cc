// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/sync_internals_message_handler.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "chrome/test/base/testing_profile.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/sync/driver/about_sync_util.h"
#include "components/sync/driver/fake_sync_service.h"
#include "components/sync/driver/sync_service.h"
#include "components/sync/js/js_test_util.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_web_ui.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::DictionaryValue;
using base::ListValue;
using base::Value;
using syncer::SyncService;
using syncer::SyncServiceObserver;
using syncer::TypeDebugInfoObserver;

namespace {

class TestableSyncInternalsMessageHandler : public SyncInternalsMessageHandler {
 public:
  TestableSyncInternalsMessageHandler(
      content::WebUI* web_ui,
      SyncServiceProvider sync_service_provider,
      AboutSyncDataDelegate about_sync_data_delegate)
      : SyncInternalsMessageHandler(std::move(sync_service_provider),
                                    std::move(about_sync_data_delegate)) {
    set_web_ui(web_ui);
  }
};

class TestSyncService : public syncer::FakeSyncService {
 public:
  void AddObserver(SyncServiceObserver* observer) override {
    ++add_observer_count_;
  }

  void RemoveObserver(SyncServiceObserver* observer) override {
    ++remove_observer_count_;
  }

  void AddTypeDebugInfoObserver(TypeDebugInfoObserver* observer) override {
    ++add_type_debug_info_observer_count_;
  }

  void RemoveTypeDebugInfoObserver(TypeDebugInfoObserver* observer) override {
    ++remove_type_debug_info_observer_count_;
  }

  base::WeakPtr<syncer::JsController> GetJsController() override {
    return js_controller_.AsWeakPtr();
  }

  void GetAllNodes(const base::Callback<void(std::unique_ptr<base::ListValue>)>&
                       callback) override {
    get_all_nodes_callback_ = std::move(callback);
  }

  int add_observer_count() const { return add_observer_count_; }
  int remove_observer_count() const { return remove_observer_count_; }
  int add_type_debug_info_observer_count() const {
    return add_type_debug_info_observer_count_;
  }
  int remove_type_debug_info_observer_count() const {
    return remove_type_debug_info_observer_count_;
  }
  base::Callback<void(std::unique_ptr<base::ListValue>)>
  get_all_nodes_callback() {
    return std::move(get_all_nodes_callback_);
  }

 private:
  int add_observer_count_ = 0;
  int remove_observer_count_ = 0;
  int add_type_debug_info_observer_count_ = 0;
  int remove_type_debug_info_observer_count_ = 0;
  syncer::MockJsController js_controller_;
  base::Callback<void(std::unique_ptr<base::ListValue>)>
      get_all_nodes_callback_;
};

class SyncInternalsMessageHandlerTest : public ::testing::Test {
 protected:
  SyncInternalsMessageHandlerTest() {
    site_instance_ = content::SiteInstance::Create(&profile_);
    web_contents_.reset(content::WebContents::Create(
        content::WebContents::CreateParams(&profile_, site_instance_.get())));
    web_ui_.set_web_contents(web_contents_.get());
    test_sync_service_ = base::MakeUnique<TestSyncService>();
    handler_.reset(new TestableSyncInternalsMessageHandler(
        &web_ui_,
        base::BindRepeating(&SyncInternalsMessageHandlerTest::sync_service,
                            base::Unretained(this)),
        base::BindRepeating(
            &SyncInternalsMessageHandlerTest::ConstructAboutInformation,
            base::Unretained(this))));
  }

  std::unique_ptr<DictionaryValue> ConstructAboutInformation(
      SyncService* service,
      version_info::Channel channel) {
    ++about_sync_data_delegate_call_count_;
    last_delegate_sync_service_ = service;
    auto dictionary = base::MakeUnique<DictionaryValue>();
    dictionary->SetString("fake_key", "fake_value");
    return dictionary;
  }

  void ValidateAboutInfoCall() {
    const auto& data_vector = web_ui_.call_data();
    ASSERT_FALSE(data_vector.empty());
    EXPECT_EQ(1u, data_vector.size());

    const content::TestWebUI::CallData& call_data = *data_vector[0];

    EXPECT_EQ(syncer::sync_ui_util::kDispatchEvent, call_data.function_name());

    const Value* arg1 = call_data.arg1();
    ASSERT_TRUE(arg1);
    std::string event_type;
    EXPECT_TRUE(arg1->GetAsString(&event_type));
    EXPECT_EQ(syncer::sync_ui_util::kOnAboutInfoUpdated, event_type);

    const Value* arg2 = call_data.arg2();
    ASSERT_TRUE(arg2);

    const DictionaryValue* root_dictionary = nullptr;
    ASSERT_TRUE(arg2->GetAsDictionary(&root_dictionary));

    std::string fake_value;
    EXPECT_TRUE(root_dictionary->GetString("fake_key", &fake_value));
    EXPECT_EQ("fake_value", fake_value);
  }

  void ValidateEmptyAboutInfoCall() {
    EXPECT_TRUE(web_ui_.call_data().empty());
  }

  TestSyncService* test_sync_service() { return test_sync_service_.get(); }

  TestableSyncInternalsMessageHandler* handler() { return handler_.get(); }

  int CallCountWithName(const std::string& function_name) {
    int count = 0;
    for (const auto& call_data : web_ui_.call_data()) {
      if (call_data->function_name() == function_name) {
        count++;
      }
    }
    return count;
  }

  int about_sync_data_delegate_call_count() const {
    return about_sync_data_delegate_call_count_;
  }

  const SyncService* last_delegate_sync_service() const {
    return last_delegate_sync_service_;
  }

  void ResetSyncService() { test_sync_service_.reset(); }

  void ResetHandler() { handler_.reset(); }

 private:
  SyncService* sync_service() { return test_sync_service_.get(); }

  content::TestBrowserThreadBundle thread_bundle_;
  TestingProfile profile_;
  content::TestWebUI web_ui_;
  scoped_refptr<content::SiteInstance> site_instance_;
  std::unique_ptr<content::WebContents> web_contents_;
  std::unique_ptr<TestSyncService> test_sync_service_;
  std::unique_ptr<TestableSyncInternalsMessageHandler> handler_;

  int about_sync_data_delegate_call_count_ = 0;
  SyncService* last_delegate_sync_service_ = nullptr;
};

TEST_F(SyncInternalsMessageHandlerTest, AddRemoveObservers) {
  ListValue empty_list;

  EXPECT_EQ(0, test_sync_service()->add_observer_count());
  handler()->HandleRegisterForEvents(&empty_list);
  EXPECT_EQ(1, test_sync_service()->add_observer_count());

  EXPECT_EQ(0, test_sync_service()->add_type_debug_info_observer_count());
  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  EXPECT_EQ(1, test_sync_service()->add_type_debug_info_observer_count());

  EXPECT_EQ(0, test_sync_service()->remove_observer_count());
  EXPECT_EQ(0, test_sync_service()->remove_type_debug_info_observer_count());
  ResetHandler();
  EXPECT_EQ(1, test_sync_service()->remove_observer_count());
  EXPECT_EQ(1, test_sync_service()->remove_type_debug_info_observer_count());

  // Add calls should never have increased since the initial subscription.
  EXPECT_EQ(1, test_sync_service()->add_observer_count());
  EXPECT_EQ(1, test_sync_service()->add_type_debug_info_observer_count());
}

TEST_F(SyncInternalsMessageHandlerTest, AddRemoveObserversDisallowJavascript) {
  ListValue empty_list;

  EXPECT_EQ(0, test_sync_service()->add_observer_count());
  handler()->HandleRegisterForEvents(&empty_list);
  EXPECT_EQ(1, test_sync_service()->add_observer_count());

  EXPECT_EQ(0, test_sync_service()->add_type_debug_info_observer_count());
  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  EXPECT_EQ(1, test_sync_service()->add_type_debug_info_observer_count());

  EXPECT_EQ(0, test_sync_service()->remove_observer_count());
  EXPECT_EQ(0, test_sync_service()->remove_type_debug_info_observer_count());
  handler()->DisallowJavascript();
  EXPECT_EQ(1, test_sync_service()->remove_observer_count());
  EXPECT_EQ(1, test_sync_service()->remove_type_debug_info_observer_count());

  // Deregistration should not repeat, no counts should increase.
  ResetHandler();
  EXPECT_EQ(1, test_sync_service()->add_observer_count());
  EXPECT_EQ(1, test_sync_service()->add_type_debug_info_observer_count());
  EXPECT_EQ(1, test_sync_service()->remove_observer_count());
  EXPECT_EQ(1, test_sync_service()->remove_type_debug_info_observer_count());
}

TEST_F(SyncInternalsMessageHandlerTest, AddRemoveObserversSyncDisabled) {
  // Simulate completely disabling sync by flag or other mechanism.
  ResetSyncService();

  ListValue empty_list;
  handler()->HandleRegisterForEvents(&empty_list);
  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  handler()->DisallowJavascript();
  // Cannot verify observer methods on sync services were not called, because
  // there is no sync service. Rather, we're just making sure the handler hasn't
  // performed any invalid operations when the sync service is missing.
}

TEST_F(SyncInternalsMessageHandlerTest,
       RepeatedHandleRegisterForPerTypeCounters) {
  ListValue empty_list;
  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  EXPECT_EQ(1, test_sync_service()->add_type_debug_info_observer_count());
  EXPECT_EQ(0, test_sync_service()->remove_type_debug_info_observer_count());

  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  EXPECT_EQ(2, test_sync_service()->add_type_debug_info_observer_count());
  EXPECT_EQ(1, test_sync_service()->remove_type_debug_info_observer_count());

  handler()->HandleRegisterForPerTypeCounters(&empty_list);
  EXPECT_EQ(3, test_sync_service()->add_type_debug_info_observer_count());
  EXPECT_EQ(2, test_sync_service()->remove_type_debug_info_observer_count());

  ResetHandler();
  EXPECT_EQ(3, test_sync_service()->add_type_debug_info_observer_count());
  EXPECT_EQ(3, test_sync_service()->remove_type_debug_info_observer_count());
}

TEST_F(SyncInternalsMessageHandlerTest, HandleGetAllNodes) {
  ListValue args;
  args.AppendInteger(0);
  handler()->HandleGetAllNodes(&args);
  test_sync_service()->get_all_nodes_callback().Run(
      base::MakeUnique<ListValue>());
  EXPECT_EQ(1, CallCountWithName(syncer::sync_ui_util::kGetAllNodesCallback));

  handler()->HandleGetAllNodes(&args);
  // This  breaks the weak ref the callback is hanging onto. Which results in
  // the call count not incrementing.
  handler()->DisallowJavascript();
  test_sync_service()->get_all_nodes_callback().Run(
      base::MakeUnique<ListValue>());
  EXPECT_EQ(1, CallCountWithName(syncer::sync_ui_util::kGetAllNodesCallback));

  handler()->HandleGetAllNodes(&args);
  test_sync_service()->get_all_nodes_callback().Run(
      base::MakeUnique<ListValue>());
  EXPECT_EQ(2, CallCountWithName(syncer::sync_ui_util::kGetAllNodesCallback));
}

TEST_F(SyncInternalsMessageHandlerTest, SendAboutInfo) {
  handler()->AllowJavascriptForTesting();
  handler()->OnStateChanged(nullptr);
  EXPECT_EQ(1, about_sync_data_delegate_call_count());
  EXPECT_NE(nullptr, last_delegate_sync_service());
  ValidateAboutInfoCall();
}

TEST_F(SyncInternalsMessageHandlerTest, SendAboutInfoSyncDisabled) {
  // Simulate completely disabling sync by flag or other mechanism.
  ResetSyncService();

  handler()->AllowJavascriptForTesting();
  handler()->OnStateChanged(nullptr);
  EXPECT_EQ(1, about_sync_data_delegate_call_count());
  EXPECT_EQ(nullptr, last_delegate_sync_service());
  ValidateAboutInfoCall();
}

}  // namespace
