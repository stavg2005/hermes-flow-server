
#include "Listener.hpp"
#include "Router.hpp"
#include <memory>
class ServerContext : std::enable_shared_from_this<ServerContext> {

public:
  explicit ServerContext(std::unique_ptr<Router> router) {
    router_ = std::move(router_);
  }

  std::unique_ptr<Router> router_;
};