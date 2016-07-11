/*****************************************************************************
 * GetItemRequest.h : GetItemRequest headers
 *
 *****************************************************************************
 * Copyright (C) 2016-2016 VideoLAN
 *
 * Authors: Paweł Wegner <pawel.wegner95@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef GETITEMREQUEST_H
#define GETITEMREQUEST_H

#include "ListDirectoryRequest.h"

namespace cloudstorage {

class GetItemRequest : public Request<IItem::Pointer> {
 public:
  using Callback = GetItemCallback;

  GetItemRequest(std::shared_ptr<CloudProvider>, const std::string& path,
                 Callback callback);
  ~GetItemRequest();

  void cancel();

 private:
  IItem::Pointer getItem(const std::vector<IItem::Pointer>& items,
                         const std::string& name) const;

  std::mutex mutex_;
  ListDirectoryRequest::Pointer current_request_;
  std::string path_;
  Callback callback_;
};

}  // namespace cloudstorage

#endif  // GETITEMREQUEST_H
