/*****************************************************************************
 * CloudFactory.cpp
 *
 *****************************************************************************
 * Copyright (C) 2016-2019 VideoLAN
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
#include "CloudFactory.h"
#include "HttpServer.h"
#include "Utility/Utility.h"

#include <json/json.h>

namespace cloudstorage {

namespace {
struct HttpWrapper : public IHttp {
  HttpWrapper(std::shared_ptr<IHttp> http) : http_(http) {}

  IHttpRequest::Pointer create(const std::string& url,
                               const std::string& method,
                               bool follow_redirect) const override {
    return http_->create(url, method, follow_redirect);
  }

  std::shared_ptr<IHttp> http_;
};

struct CryptoWrapper : public ICrypto {
  CryptoWrapper(std::shared_ptr<ICrypto> crypto) : crypto_(crypto) {}

  std::string sha256(const std::string& message) override {
    return crypto_->sha256(message);
  }
  std::string hmac_sha256(const std::string& key,
                          const std::string& message) override {
    return crypto_->hmac_sha256(key, message);
  }
  std::string hmac_sha1(const std::string& key,
                        const std::string& message) override {
    return crypto_->hmac_sha1(key, message);
  }

  std::string hex(const std::string& hash) override {
    return crypto_->hex(hash);
  }

  std::shared_ptr<ICrypto> crypto_;
};

struct ThreadPoolWrapper : public IThreadPool {
  ThreadPoolWrapper(std::shared_ptr<IThreadPool> pool) : thread_pool_(pool) {}

  void schedule(const Task& f) override { thread_pool_->schedule(f); }

  std::shared_ptr<IThreadPool> thread_pool_;
};

struct AuthCallback : public ICloudProvider::IAuthCallback {
  AuthCallback(CloudFactory* factory) : factory_(factory) {}

  Status userConsentRequired(const ICloudProvider&) override {
    return Status::None;
  }

  void done(const ICloudProvider& p, EitherError<void> e) override {
    if (e.left()) {
      factory_->onCloudRemoved(p);
    }
  }

  CloudFactory* factory_;
};

struct HttpServerFactoryWrapper : public IHttpServerFactory {
  HttpServerFactoryWrapper(std::shared_ptr<IHttpServerFactory> factory)
      : factory_(factory) {}

  IHttpServer::Pointer create(IHttpServer::ICallback::Pointer cb,
                              const std::string& session_id,
                              IHttpServer::Type type) override {
    return factory_->create(cb, session_id, type);
  }

 private:
  std::shared_ptr<IHttpServerFactory> factory_;
};

struct HttpCallback : public IHttpServer::ICallback {
  HttpCallback(CloudFactory* factory) : factory_(factory) {}

  IHttpServer::IResponse::Pointer handle(
      const IHttpServer::IRequest& request) override {
    auto state = first_url_part(request.url());
    const char* code = request.get("code");
    if (code) {
      CloudFactory::ProviderInitData data;
      data.permission_ = ICloudProvider::Permission::ReadWrite;
      auto access = std::make_shared<CloudAccess>(
          factory_->create(state, std::move(data)));
      factory_->add(access->provider()->exchangeCodeAsync(
          code, [factory = factory_, state,
                 access = std::move(access)](EitherError<Token> e) {
            factory->invoke([factory, state, e, access = std::move(access)] {
              factory->onCloudTokenReceived(state, e);
            });
          }));
    }
    if (request.url().find("/login") != std::string::npos) {
      return util::response_from_string(request, 200, {},
                                        util::login_page(state));
    }

    return util::response_from_string(request, 200, {}, "ok");
  }

  CloudFactory* factory_;
};

uint64_t cloud_identifier(const ICloudProvider& p) {
  auto state = p.hints()["state"];
  return std::stoull(state.substr(p.name().length() + 1));
}

}  // namespace

CloudFactory::CloudFactory(CloudEventLoop* event_loop,
                           CloudFactory::InitData&& d)
    : base_url_(d.base_url_),
      http_(std::move(d.http_)),
      http_server_factory_(util::make_unique<ServerWrapperFactory>(
          d.http_server_factory_.get())),
      crypto_(std::move(d.crypto_)),
      thread_pool_(std::move(d.thread_pool_)),
      cloud_storage_(ICloudStorage::create()),
      provider_index_(),
      loop_(event_loop->impl()) {
  for (const auto& d : cloud_storage_->providers()) {
    http_server_handles_.emplace_back(
        http_server_factory_->create(util::make_unique<HttpCallback>(this), d,
                                     IHttpServer::Type::Authorization));
  }
}

CloudFactory::~CloudFactory() {
  loop_->clear();
  loop_->process_events();
  http_server_handles_.clear();
  cloud_access_.clear();
}

CloudAccess CloudFactory::create(
    const std::string& provider_name,
    const CloudFactory::ProviderInitData& data) const {
  ICloudProvider::InitData init_data;
  init_data.token_ = std::move(data.token_);
  init_data.hints_ = std::move(data.hints_);
  init_data.permission_ = data.permission_;
  init_data.http_engine_ =
      http_ ? util::make_unique<HttpWrapper>(http_) : nullptr;
  init_data.http_server_ =
      http_server_factory_
          ? util::make_unique<HttpServerFactoryWrapper>(http_server_factory_)
          : nullptr;
  init_data.crypto_engine_ =
      crypto_ ? util::make_unique<CryptoWrapper>(crypto_) : nullptr;
  init_data.thread_pool_ =
      thread_pool_ ? util::make_unique<ThreadPoolWrapper>(thread_pool_)
                   : nullptr;
  init_data.callback_ =
      util::make_unique<AuthCallback>(const_cast<CloudFactory*>(this));
  auto index = provider_index_++;
  auto state = provider_name + "-" + std::to_string(index);
  init_data.hints_["state"] = state;
  init_data.hints_["file_url"] =
      base_url_ + (base_url_.back() == '/' ? "" : "/") + state;
  init_data.hints_["redirect_uri"] =
      base_url_ + (base_url_.back() == '/' ? "" : "/") + provider_name;
  return CloudAccess(
      loop_, cloud_storage_->provider(provider_name, std::move(init_data)));
}

void CloudFactory::add(std::unique_ptr<IGenericRequest>&& request) {
  loop_->add(loop_->next_tag(), std::move(request));
}

void CloudFactory::invoke(const std::function<void()>& f) { loop_->invoke(f); }

void CloudFactory::onCloudTokenReceived(const std::string& provider,
                                        const EitherError<Token>& token) {
  if (token.right()) {
    ProviderInitData init_data;
    init_data.token_ = token.right()->token_;
    init_data.hints_["access_token"] = token.right()->access_token_;
    auto cloud_access =
        std::make_shared<CloudAccess>(create(provider, init_data));
    cloud_access_.insert(
        {cloud_identifier(*cloud_access->provider()), cloud_access});
    onCloudCreated(cloud_access);
  } else {
    util::log("exchange token failed", token.left()->description_);
  }
}

void CloudFactory::onCloudCreated(std::shared_ptr<CloudAccess>) {}

void CloudFactory::onCloudRemoved(std::shared_ptr<CloudAccess>) {}

std::string CloudFactory::authorizationUrl(const std::string& provider) const {
  ProviderInitData data;
  data.permission_ = ICloudProvider::Permission::ReadWrite;
  auto p = create(provider, data);
  return p.provider()->authorizeLibraryUrl();
}

std::vector<std::string> CloudFactory::availableProviders() const {
  return cloud_storage_->providers();
}

void CloudFactory::onCloudRemoved(const ICloudProvider& d) {
  auto identifier = cloud_identifier(d);
  loop_->invoke([=] {
    auto it = cloud_access_.find(identifier);
    if (it != cloud_access_.end()) {
      onCloudRemoved(it->second);
      cloud_access_.erase(it);
    }
  });
}

void CloudFactory::dump(std::ostream& stream) {
  try {
    Json::Value json;
    Json::Value providers;
    for (const auto& d : cloud_access_) {
      Json::Value provider;
      provider["type"] = d.second->provider()->name();
      provider["token"] = d.second->provider()->token();
      provider["access_token"] = d.second->provider()->hints()["access_token"];
      providers.append(provider);
    }
    json["providers"] = providers;
    stream << json;
  } catch (const Json::Exception&) {
  }
}

void CloudFactory::load(std::istream& stream) {
  try {
    Json::Value json;
    stream >> json;
    for (const auto& d : json["providers"]) {
      ProviderInitData data;
      data.token_ = d["token"].asString();
      data.hints_["access_token"] = d["access_token"].asString();
      auto cloud =
          std::make_shared<CloudAccess>(create(d["type"].asString(), data));
      onCloudCreated(cloud);
      cloud_access_.insert({cloud_identifier(*cloud->provider()), cloud});
    }
  } catch (const Json::Exception&) {
  }
}

std::vector<std::shared_ptr<CloudAccess>> CloudFactory::providers() const {
  std::vector<std::shared_ptr<CloudAccess>> result;
  for (const auto& p : cloud_access_) {
    result.push_back(p.second);
  }
  return result;
}

}  // namespace cloudstorage