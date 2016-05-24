/*****************************************************************************
 * CloudProvider.cpp : implementation of CloudProvider
 *
 *****************************************************************************
 * Copyright (C) 2016-2016 VideoLAN
 *
 * Authors: Paweł Wegner <pawel.wegner95@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "CloudProvider.h"

#include <jsoncpp/json/json.h>

#include "Item.h"
#include "Utility.h"

namespace cloudstorage {

namespace {
class InitCallback : public IAuth::IInitCallback {
 public:
  InitCallback(ICloudProvider::IInitCallback::Pointer callback,
               const ICloudProvider& provider)
      : callback_(std::move(callback)), provider_(provider) {}

  void userConsentRequired(const IAuth&) const {
    if (callback_) callback_->userConsentRequired(provider_);
  }

 private:
  ICloudProvider::IInitCallback::Pointer callback_;
  const ICloudProvider& provider_;
};
}  // namespace

CloudProvider::CloudProvider(IAuth::Pointer auth) : auth_(std::move(auth)) {}

bool CloudProvider::initialize(IInitCallback::Pointer callback) {
  return auth()->authorize(
      make_unique<cloudstorage::InitCallback>(std::move(callback), *this));
}

Json::Value CloudProvider::dump() const {
  Json::Value data;
  data["backend"] = name();
  data["token"] = auth()->token_data();
  return data;
}

std::string CloudProvider::access_token() const {
  return auth()->access_token()->token_;
}

IAuth* CloudProvider::auth() const { return auth_.get(); }

std::vector<IItem::Pointer> CloudProvider::listDirectory(
    const IItem& item) const {
  return execute(&CloudProvider::executeListDirectory, item);
}

void CloudProvider::uploadFile(const std::string& filename,
                               std::istream& stream) const {
  execute(&CloudProvider::executeUploadFile, filename, stream);
}

void CloudProvider::downloadFile(const IItem& item,
                                 std::ostream& stream) const {
  execute(&CloudProvider::executeDownloadFile, item, stream);
}

std::string CloudProvider::authorizeLibraryUrl() const {
  return auth()->authorizeLibraryUrl();
}

IItem::Pointer CloudProvider::rootDirectory() const {
  return make_unique<Item>("/", "root", true);
}

}  // namespace cloudstorage