// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_SHELL_PUBLIC_CPP_SERVICE_TEST_H_
#define SERVICES_SHELL_PUBLIC_CPP_SERVICE_TEST_H_

#include <memory>

#include "base/macros.h"
#include "services/shell/public/cpp/connector.h"
#include "services/shell/public/cpp/service.h"
#include "services/shell/public/cpp/service_context.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
class MessageLoop;
}

namespace shell {

class BackgroundShell;

namespace test {

class ServiceTest;

// A default implementation of Service for use in ServiceTests. Tests wishing
// to customize this should subclass this class instead of Service,
// otherwise they will have to call ServiceTest::OnStartCalled() to forward
// metadata from OnStart() to the test.
class ServiceTestClient : public Service {
 public:
  explicit ServiceTestClient(ServiceTest* test);
  ~ServiceTestClient() override;

 protected:
  void OnStart(const Identity& identity) override;

 private:
  ServiceTest* test_;

  DISALLOW_COPY_AND_ASSIGN(ServiceTestClient);
};

class ServiceTest : public testing::Test {
 public:
  ServiceTest();
  // Initialize passing the name to use as the identity for the test itself.
  // Once set via this constructor, it cannot be changed later by calling
  // InitTestName(). The test executable must provide a manifest in the
  // appropriate location that specifies this name also.
  explicit ServiceTest(const std::string& test_name);
  ~ServiceTest() override;

 protected:
  // See constructor. Can only be called once.
  void InitTestName(const std::string& test_name);

  Connector* connector() { return connector_; }

  // Instance information received from the Service Manager during OnStart().
  const std::string& test_name() const { return initialize_name_; }
  const std::string& test_userid() const { return initialize_userid_; }
  uint32_t test_instance_id() const { return initialize_instance_id_; }

  // By default, creates a simple Service that captures the metadata sent
  // via OnStart(). Override to customize, but custom implementations must
  // call OnStartCalled() to forward the metadata so test_name() etc all
  // work.
  virtual std::unique_ptr<Service> CreateService();

  virtual std::unique_ptr<base::MessageLoop> CreateMessageLoop();

  // Call to set OnStart() metadata when GetService() is overridden.
  void OnStartCalled(Connector* connector,
                     const std::string& name,
                     const std::string& userid);

  // testing::Test:
  void SetUp() override;
  void TearDown() override;

 private:
  friend ServiceTestClient;

  std::unique_ptr<Service> service_;

  std::unique_ptr<base::MessageLoop> message_loop_;
  std::unique_ptr<BackgroundShell> background_shell_;
  std::unique_ptr<ServiceContext> service_context_;

  // See constructor.
  std::string test_name_;

  Connector* connector_ = nullptr;
  std::string initialize_name_;
  std::string initialize_userid_ = shell::mojom::kInheritUserID;
  uint32_t initialize_instance_id_ = shell::mojom::kInvalidInstanceID;

  base::Closure initialize_called_;

  DISALLOW_COPY_AND_ASSIGN(ServiceTest);
};

}  // namespace test
}  // namespace shell

#endif  // SERVICES_SHELL_PUBLIC_CPP_SERVICE_TEST_H_
