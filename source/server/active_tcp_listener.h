#pragma once

#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/stream_info/stream_info_impl.h"

#include "server/active_listener_base.h"
#include "server/active_stream_socket.h"

namespace Envoy {
namespace Server {

struct ActiveTcpConnection;
using ActiveTcpConnectionPtr = std::unique_ptr<ActiveTcpConnection>;
class ActiveConnections;
using ActiveConnectionsPtr = std::unique_ptr<ActiveConnections>;

namespace {
// Structure used to allow a unique_ptr to be captured in a posted lambda. See below.
struct RebalancedSocket {
  Network::ConnectionSocketPtr socket;
};
using RebalancedSocketSharedPtr = std::shared_ptr<RebalancedSocket>;
} // namespace

/**
 * Wrapper for an active tcp listener owned by this handler.
 */
class ActiveTcpListener final : public Network::TcpListenerCallbacks,
                                public ActiveStreamListenerBase,
                                public Network::BalancedConnectionHandler,
                                Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveTcpListener(Network::TcpConnectionHandler& parent, Network::ListenerConfig& config);
  ~ActiveTcpListener() override;
  bool listenerConnectionLimitReached() const {
    // TODO(tonya11en): Delegate enforcement of per-listener connection limits to overload
    // manager.
    return !config_->openConnections().canCreate();
  }

  void decNumConnections() override {
    ASSERT(num_listener_connections_ > 0);
    --num_listener_connections_;
    config_->openConnections().dec();
  }

  // Network::TcpListenerCallbacks
  void onAccept(Network::ConnectionSocketPtr&& socket) override;
  void onReject(RejectCause) override;

  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) override;

  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener() override { listener_.reset(); }

  // Network::BalancedConnectionHandler
  uint64_t numConnections() const override { return num_listener_connections_; }
  void incNumConnections() override {
    ++num_listener_connections_;
    config_->openConnections().inc();
  }
  void post(Network::ConnectionSocketPtr&& socket) override;
  void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                      bool hand_off_restored_destination_connections, bool rebalanced) override;

  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveTcpConnection& connection);

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info) override;

  /**
   * Return the active connections container attached with the given filter chain.
   */
  ActiveConnections& getOrCreateActiveConnections(const Network::FilterChain& filter_chain);

  /**
   * Schedule to remove and destroy the active connections which are not tracked by listener
   * config. Caution: The connection are not destroyed yet when function returns.
   */
  void
  deferredRemoveFilterChains(const std::list<const Network::FilterChain*>& draining_filter_chains);

  /**
   * Update the listener config. The follow up connections will see the new config. The existing
   * connections are not impacted.
   */
  void updateListenerConfig(Network::ListenerConfig& config);

  absl::node_hash_map<const Network::FilterChain*, ActiveConnectionsPtr> connections_by_context_;

  Network::TcpConnectionHandler& tcp_conn_handler_;
  // The number of connections currently active on this listener. This is typically used for
  // connection balancing across per-handler listeners.
  std::atomic<uint64_t> num_listener_connections_{};
  bool is_deleting_{false};
};

/**
 * Wrapper for a group of active connections which are attached to the same filter chain context.
 */
class ActiveConnections : public Event::DeferredDeletable {
public:
  ActiveConnections(ActiveTcpListener& listener, const Network::FilterChain& filter_chain);
  ~ActiveConnections() override;

  // listener filter chain pair is the owner of the connections
  ActiveTcpListener& listener_;
  const Network::FilterChain& filter_chain_;
  // Owned connections
  std::list<ActiveTcpConnectionPtr> connections_;
};

/**
 * Wrapper for an active TCP connection owned by this handler.
 */
struct ActiveTcpConnection : LinkedObject<ActiveTcpConnection>,
                             public Event::DeferredDeletable,
                             public Network::ConnectionCallbacks {
  ActiveTcpConnection(ActiveConnections& active_connections,
                      Network::ConnectionPtr&& new_connection, TimeSource& time_system,
                      std::unique_ptr<StreamInfo::StreamInfo>&& stream_info);
  ~ActiveTcpConnection() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    // Any event leads to destruction of the connection.
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      active_connections_.listener_.removeConnection(*this);
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  ActiveConnections& active_connections_;
  Network::ConnectionPtr connection_;
  Stats::TimespanPtr conn_length_;
};

using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;

} // namespace Server
} // namespace Envoy
