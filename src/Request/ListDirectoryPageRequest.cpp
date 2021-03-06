/*****************************************************************************
 * ListDirectoryPageRequest.cpp
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

#include "ListDirectoryPageRequest.h"

#include "CloudProvider/CloudProvider.h"

namespace cloudstorage {

ListDirectoryPageRequest::ListDirectoryPageRequest(
    std::shared_ptr<CloudProvider> p, IItem::Pointer directory,
    const std::string& token, ListDirectoryPageCallback completed)
    : Request(p, completed, [=](Request<EitherError<PageData>>::Pointer r) {
        if (directory->type() != IItem::FileType::Directory)
          return r->done(
              Error{IHttpRequest::Bad, util::Error::NOT_A_DIRECTORY});
        r->request(
            [=](util::Output input) {
              return r->provider()->listDirectoryRequest(*directory, token,
                                                         *input);
            },
            [=](EitherError<Response> e) {
              if (e.left()) return r->done(e.left());
              try {
                std::string next_token;
                auto lst = r->provider()->listDirectoryResponse(
                    *directory, e.right()->output(), next_token);
                r->done(PageData{lst, next_token});
              } catch (const std::exception& e) {
                r->done(Error{IHttpRequest::Failure, e.what()});
              }
            });
      }) {}

}  // namespace cloudstorage
