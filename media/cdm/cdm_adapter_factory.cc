// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cdm/cdm_adapter_factory.h"

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/threading/thread_task_runner_handle.h"
#include "media/base/cdm_factory.h"
#include "media/base/key_systems.h"
#include "media/cdm/cdm_adapter.h"
#include "media/cdm/cdm_paths.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"

namespace media {

CdmAdapterFactory::CdmAdapterFactory(
    CdmAllocator::CreationCB allocator_creation_cb)
    : allocator_creation_cb_(std::move(allocator_creation_cb)) {
  DCHECK(allocator_creation_cb_);
}

CdmAdapterFactory::~CdmAdapterFactory() {}

void CdmAdapterFactory::Create(
    const std::string& key_system,
    const GURL& security_origin,
    const CdmConfig& cdm_config,
    const SessionMessageCB& session_message_cb,
    const SessionClosedCB& session_closed_cb,
    const SessionKeysChangeCB& session_keys_change_cb,
    const SessionExpirationUpdateCB& session_expiration_update_cb,
    const CdmCreatedCB& cdm_created_cb) {
  DVLOG(1) << __FUNCTION__ << ": key_system=" << key_system;

  if (!security_origin.is_valid()) {
    LOG(ERROR) << "Invalid Origin: " << security_origin;
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(cdm_created_cb, nullptr, "Invalid origin."));
    return;
  }

  base::FilePath cdm_path;

#if defined(WIDEVINE_CDM_AVAILABLE)
  // TODO(xhwang): Remove key-system-specific logic here. We should have the
  // CDM path forwarded from the browser already. See http://crbug.com/510604
  if (key_system == kWidevineKeySystem) {
    // Build the library path for Widevine CDM.
    // TODO(xhwang): We should have the CDM path forwarded from the browser
    // already. See http://crbug.com/510604
    base::FilePath cdm_base_path;
    base::PathService::Get(base::DIR_MODULE, &cdm_base_path);
    cdm_base_path = cdm_base_path.Append(
        GetPlatformSpecificDirectory(kWidevineCdmBaseDirectory));
    cdm_path = cdm_base_path.AppendASCII(
        base::GetNativeLibraryName(kWidevineCdmLibraryName));
  }
#endif  // defined(WIDEVINE_CDM_AVAILABLE)

  if (cdm_path.empty()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(cdm_created_cb, nullptr, "Unsupported key system."));
    return;
  }

  std::unique_ptr<CdmAllocator> cdm_allocator = allocator_creation_cb_.Run();
  if (!cdm_allocator) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(cdm_created_cb, nullptr, "CDM allocator creation failed."));
    return;
  }

  // TODO(xhwang): Hook up auxiliary services, e.g. File IO, output protection,
  // and platform verification.
  CdmAdapter::Create(key_system, cdm_path, cdm_config, std::move(cdm_allocator),
                     CreateCdmFileIOCB(), session_message_cb, session_closed_cb,
                     session_keys_change_cb, session_expiration_update_cb,
                     cdm_created_cb);
}

}  // namespace media
