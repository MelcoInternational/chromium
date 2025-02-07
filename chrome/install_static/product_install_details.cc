// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/install_static/product_install_details.h"

#include <windows.h>

#include <algorithm>

#include "chrome/install_static/install_details.h"
#include "chrome/install_static/install_modes.h"
#include "chrome/install_static/install_util.h"
#include "chrome/install_static/user_data_dir.h"
#include "chrome_elf/nt_registry/nt_registry.h"

namespace install_static {

namespace {

// Returns the executable path for the current process.
std::wstring GetCurrentProcessExePath() {
  std::wstring exe_path(MAX_PATH, L'\0');
  DWORD length = ::GetModuleFileName(nullptr, &exe_path[0], exe_path.size());
  if (!length)
    return std::wstring();
  exe_path.resize(length);
  return exe_path;
}

const InstallConstants* FindInstallMode(const std::wstring& suffix) {
  // Search for a mode with the matching suffix.
  for (int i = 0; i < NUM_INSTALL_MODES; ++i) {
    const InstallConstants& mode = kInstallModes[i];
    if (!_wcsicmp(suffix.c_str(), mode.install_suffix))
      return &mode;
  }
  // The first mode is always the default if all else fails.
  return &kInstallModes[0];
}

}  // namespace

void InitializeProductDetailsForPrimaryModule() {
  InstallDetails::SetForProcess(MakeProductDetails(GetCurrentProcessExePath()));
}

bool IsPathParentOf(const wchar_t* parent,
                    size_t parent_len,
                    const std::wstring& path) {
  // Ignore all terminating path separators in |parent|.
  while (parent_len && parent[parent_len - 1] == L'\\')
    --parent_len;
  // Pass if the parent was all separators.
  if (!parent_len)
    return false;
  // This is a match if |parent| exactly matches |path| or is followed by a
  // separator.
  return !::wcsnicmp(path.c_str(), parent, parent_len) &&
         (path.size() == parent_len || path[parent_len] == L'\\');
}

bool PathIsInProgramFiles(const std::wstring& path) {
  static constexpr const wchar_t* kProgramFilesVariables[] = {
      L"PROGRAMFILES",  // With or without " (x86)" according to exe bitness.
      L"PROGRAMFILES(X86)",  // Always "C:\Program Files (x86)"
      L"PROGRAMW6432",       // Always "C:\Program Files" under WoW64.
  };
  wchar_t value[MAX_PATH];
  *value = L'\0';
  for (const wchar_t* variable : kProgramFilesVariables) {
    *value = L'\0';
    DWORD ret = ::GetEnvironmentVariableW(variable, value, _countof(value));
    if (ret && ret < _countof(value) && IsPathParentOf(value, ret, path))
      return true;
  }

  return false;
}

std::wstring GetInstallSuffix(const std::wstring& exe_path) {
  // Search backwards from the end of the path for "\Application", using a
  // manual search for the sake of case-insensitivity.
  static constexpr wchar_t kInstallBinaryDir[] = L"\\Application";
  constexpr size_t kInstallBinaryDirLength = _countof(kInstallBinaryDir) - 1;
  if (exe_path.size() < kProductPathNameLength + kInstallBinaryDirLength)
    return std::wstring();
  std::wstring::const_reverse_iterator scan =
      exe_path.crbegin() + (kInstallBinaryDirLength - 1);
  while (_wcsnicmp(&*scan, kInstallBinaryDir, kInstallBinaryDirLength) &&
         ++scan != exe_path.crend()) {
  }
  if (scan == exe_path.crend())
    return std::wstring();

  // Ensure that the dir is followed by a separator or is at the end of the
  // path.
  if (scan - exe_path.crbegin() != kInstallBinaryDirLength - 1 &&
      *(scan - kInstallBinaryDirLength) != L'\\') {
    return std::wstring();
  }

  // Scan backwards to the next separator or the beginning of the path.
  std::wstring::const_reverse_iterator name =
      std::find(scan + 1, exe_path.crend(), L'\\');
  // Back up one character to ignore the separator/end of iteration.
  if (name == exe_path.crend())
    name = exe_path.crbegin() + exe_path.size() - 1;
  else
    --name;

  // Check for a match of the product directory name.
  if (_wcsnicmp(&*name, kProductPathName, kProductPathNameLength))
    return std::wstring();

  // Return the (possibly empty) suffix betwixt the product name and install
  // binary dir.
  return std::wstring(&*(name - kProductPathNameLength),
                      (name - scan) - kProductPathNameLength);
}

std::unique_ptr<PrimaryInstallDetails> MakeProductDetails(
    const std::wstring& exe_path) {
  std::unique_ptr<PrimaryInstallDetails> details(new PrimaryInstallDetails());

  const InstallConstants* mode = FindInstallMode(GetInstallSuffix(exe_path));
  const bool system_level =
      mode->supports_system_level && PathIsInProgramFiles(exe_path);

  details->set_mode(mode);
  details->set_system_level(system_level);

  // Cache the ap and cohort name values found in the registry for use in crash
  // keys and in about:version.
  std::wstring update_ap;
  std::wstring update_cohort_name;
  details->set_channel(DetermineChannel(*mode, system_level,
                                        false /* !from_binaries */, &update_ap,
                                        &update_cohort_name));
  details->set_update_ap(update_ap);
  details->set_update_cohort_name(update_cohort_name);

  if (CurrentProcessNeedsProfileDir()) {
    std::wstring user_data_dir;
    std::wstring invalid_user_data_dir;
    install_static::DeriveUserDataDirectory(*mode, &user_data_dir,
                                            &invalid_user_data_dir);
    details->set_user_data_dir(user_data_dir);
    details->set_invalid_user_data_dir(invalid_user_data_dir);
  }

  return details;
}

}  // namespace install_static
