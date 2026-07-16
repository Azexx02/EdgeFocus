#include "BleGattServer.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "app/SessionController.h"

#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
#include <gio/gio.h>
#endif

namespace edgefocus {

namespace {
constexpr const char* kAppPath = "/com/edgefocus/app";
constexpr const char* kServicePath = "/com/edgefocus/app/service0";
constexpr const char* kTxPath = "/com/edgefocus/app/service0/char0";
constexpr const char* kRxPath = "/com/edgefocus/app/service0/char1";
constexpr const char* kAdvPath = "/com/edgefocus/advertisement0";
constexpr const char* kAdapterPath = "/org/bluez/hci0";
constexpr const char* kServiceUuid = "0000ff00-0000-1000-8000-00805f9b34fb";
constexpr const char* kTxUuid = "0000ff01-0000-1000-8000-00805f9b34fb";
constexpr const char* kRxUuid = "0000ff02-0000-1000-8000-00805f9b34fb";
constexpr std::size_t kChunkPayloadBytes = 96;
constexpr std::size_t kDirectPayloadBytes = 180;
constexpr std::size_t kMaxBleRequestBytes = 8 * 1024;

int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string base64Encode(const std::string& input) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string chunkIdFor(bool push, int request_id) {
    return std::string(push ? "push_" : "resp_") + std::to_string(request_id) +
           "_" + std::to_string(monotonicMs());
}

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
GVariant* bytesToVariant(const std::string& payload) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
    for (unsigned char c : payload) {
        g_variant_builder_add(&builder, "y", c);
    }
    return g_variant_builder_end(&builder);
}

std::string variantBytesToString(GVariant* value) {
    std::string out;
    GVariantIter iter;
    guchar byte = 0;
    g_variant_iter_init(&iter, value);
    while (g_variant_iter_next(&iter, "y", &byte)) {
        out.push_back(static_cast<char>(byte));
    }
    return out;
}

void addStringProp(GVariantBuilder* props, const char* key, const char* value) {
    g_variant_builder_add(props, "{sv}", key, g_variant_new_string(value));
}

void addBoolProp(GVariantBuilder* props, const char* key, gboolean value) {
    g_variant_builder_add(props, "{sv}", key, g_variant_new_boolean(value));
}

void addObjectProp(GVariantBuilder* props, const char* key, const char* value) {
    g_variant_builder_add(props, "{sv}", key, g_variant_new_object_path(value));
}

void addStringArrayProp(GVariantBuilder* props, const char* key,
                        const std::vector<const char*>& values) {
    GVariantBuilder arr;
    g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
    for (const char* value : values) {
        g_variant_builder_add(&arr, "s", value);
    }
    g_variant_builder_add(props, "{sv}", key, g_variant_builder_end(&arr));
}

void addBytesProp(GVariantBuilder* props, const char* key, const std::string& payload) {
    g_variant_builder_add(props, "{sv}", key, bytesToVariant(payload));
}

GVariant* buildServiceProperties() {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    addStringProp(&props, "UUID", kServiceUuid);
    addBoolProp(&props, "Primary", TRUE);
    return g_variant_builder_end(&props);
}

GVariant* buildTxProperties() {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    addStringProp(&props, "UUID", kTxUuid);
    addObjectProp(&props, "Service", kServicePath);
    addStringArrayProp(&props, "Flags", {"write", "write-without-response"});
    return g_variant_builder_end(&props);
}

GVariant* buildRxProperties(const std::string& payload, bool notifying) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    addStringProp(&props, "UUID", kRxUuid);
    addObjectProp(&props, "Service", kServicePath);
    addStringArrayProp(&props, "Flags", {"read", "notify"});
    addBoolProp(&props, "Notifying", notifying ? TRUE : FALSE);
    addBytesProp(&props, "Value", payload);
    return g_variant_builder_end(&props);
}

GVariant* buildAdvertisementProperties() {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    addStringProp(&props, "Type", "peripheral");
    addStringProp(&props, "LocalName", "EdgeFocus");
    addStringArrayProp(&props, "ServiceUUIDs", {kServiceUuid});
    addStringArrayProp(&props, "Includes", {"tx-power"});
    return g_variant_builder_end(&props);
}
#endif
} // namespace

BleGattServer::BleGattServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                             ReminderPolicy& reminder_policy, ModeManager& mode_mgr,
                             GimbalMonitor* gimbal_monitor,
                             session::SessionOrchestrator* session_orchestrator,
                             PhoneFactBuffer* phone_facts,
                             session::SessionSnapshotPublisher* session_snapshots,
                             power::WorkloadProfilePolicy* workload_policy,
                             gimbal::GimbalPrecheckController* gimbal_precheck)
    : Module("BleGattServer")
    , store_(store)
    , sqlite_store_(sqlite_store)
    , app_commands_(store, sqlite_store, reminder_policy, mode_mgr, gimbal_monitor,
                    session_orchestrator, phone_facts, session_snapshots, &event_outbox_,
                    workload_policy, gimbal_precheck) {}

BleGattServer::~BleGattServer() = default;

bool BleGattServer::init(AppContext& /*ctx*/) {
    app_commands_.loadPreferences();

    SessionController controller(store_, sqlite_store_);
    int recovered = controller.closeInterruptedSessions();
    if (recovered > 0) {
        LOGW("BleGattServer", "Closed %d interrupted persisted session(s) during startup", recovered);
    }

    store_.setPushCallback([this](const std::string& event_json) {
        const auto queued = event_outbox_.enqueue(
            event_json, store_.snapshot().session.session_id, monotonicMs());
        if (queued.critical) {
            if (queued.send_once && phone_connected_.load()) notifyBluez(queued.payload);
            for (const auto& payload : event_outbox_.due(monotonicMs())) notifyBluez(payload);
            return;
        }
        if (phone_connected_.load()) notifyBluez(event_json);
    });
    LOGI("BleGattServer", "BLE GATT Server initialized");
    return true;
}

void BleGattServer::runOnce(AppContext& /*ctx*/) {
#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    if (!bluez_ready_) {
        bluez_ready_ = initBluez();
    }
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
#endif
    for (const auto& payload : event_outbox_.due(monotonicMs())) {
        notifyBluez(payload);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void BleGattServer::shutdown(AppContext& /*ctx*/) {
    event_outbox_.setSubscribed(false);
    shutdownBluez();
    LOGI("BleGattServer", "BLE GATT Server shutdown");
}

std::string BleGattServer::handleCommand(const std::string& json_cmd) {
    if (json_cmd.size() > kMaxBleRequestBytes) {
        return AppCommandService::makeError(0, "BLE request too large");
    }
    return app_commands_.handleCommand(json_cmd);
}

std::vector<std::string> BleGattServer::makeChunkPayloadsForTest(
    const std::string& payload,
    int request_id,
    bool push,
    std::size_t chunk_size) {
    const std::size_t safe_chunk_size = std::max<std::size_t>(1, chunk_size);
    const int total = static_cast<int>((payload.size() + safe_chunk_size - 1) / safe_chunk_size);
    const std::string chunk_id = chunkIdFor(push, request_id);
    std::vector<std::string> chunks;
    chunks.reserve(total);

    for (int seq = 0; seq < total; ++seq) {
        const std::size_t offset = static_cast<std::size_t>(seq) * safe_chunk_size;
        const auto part = payload.substr(offset, safe_chunk_size);
        nlohmann::json chunk_data = {
            {"chunk_id", chunk_id},
            {"seq", seq},
            {"total", total},
            {"encoding", "base64_utf8"},
            {"data", base64Encode(part)}
        };
        if (push) {
            chunks.push_back(nlohmann::json{{"cmd", "push_chunk"}, {"data", chunk_data}}.dump());
        } else {
            chunk_data["status"] = "chunk";
            chunk_data["id"] = request_id;
            chunks.push_back(chunk_data.dump());
        }
    }
    return chunks;
}

// ---------------------------------------------------------------------------
// Push events
// ---------------------------------------------------------------------------

void BleGattServer::pushEvent(const std::string& event_type, const std::string& json_data) {
    if (!phone_connected_.load()) return;
    LOGI("BleGattServer", "Push event: %s", event_type.c_str());
    notifyBluez(json_data);
}

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
namespace {
const char* kObjectManagerXml = R"xml(
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects">
      <arg name="objects" type="a{oa{sa{sv}}}" direction="out"/>
    </method>
  </interface>
</node>
)xml";

const char* kServiceXml = R"xml(
<node>
  <interface name="org.bluez.GattService1">
    <property name="UUID" type="s" access="read"/>
    <property name="Primary" type="b" access="read"/>
  </interface>
</node>
)xml";

const char* kCharacteristicXml = R"xml(
<node>
  <interface name="org.bluez.GattCharacteristic1">
    <method name="ReadValue">
      <arg name="options" type="a{sv}" direction="in"/>
      <arg name="value" type="ay" direction="out"/>
    </method>
    <method name="WriteValue">
      <arg name="value" type="ay" direction="in"/>
      <arg name="options" type="a{sv}" direction="in"/>
    </method>
    <method name="StartNotify"/>
    <method name="StopNotify"/>
    <property name="UUID" type="s" access="read"/>
    <property name="Service" type="o" access="read"/>
    <property name="Flags" type="as" access="read"/>
    <property name="Value" type="ay" access="read"/>
    <property name="Notifying" type="b" access="read"/>
  </interface>
</node>
)xml";

const char* kAdvertisementXml = R"xml(
<node>
  <interface name="org.bluez.LEAdvertisement1">
    <method name="Release"/>
    <property name="Type" type="s" access="read"/>
    <property name="ServiceUUIDs" type="as" access="read"/>
    <property name="LocalName" type="s" access="read"/>
    <property name="Includes" type="as" access="read"/>
  </interface>
</node>
)xml";

void addInterface(GVariantBuilder* interfaces, const char* name, GVariant* props) {
    g_variant_builder_add(interfaces, "{s@a{sv}}", name, props);
}

GVariant* buildManagedObjects(BleGattServer* server) {
    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));

    {
        GVariantBuilder interfaces;
        g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
        addInterface(&interfaces, "org.bluez.GattService1", buildServiceProperties());
        g_variant_builder_add(&objects, "{o@a{sa{sv}}}", kServicePath,
                              g_variant_builder_end(&interfaces));
    }
    {
        GVariantBuilder interfaces;
        g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
        addInterface(&interfaces, "org.bluez.GattCharacteristic1", buildTxProperties());
        g_variant_builder_add(&objects, "{o@a{sa{sv}}}", kTxPath,
                              g_variant_builder_end(&interfaces));
    }
    {
        GVariantBuilder interfaces;
        g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
        addInterface(&interfaces, "org.bluez.GattCharacteristic1",
                     buildRxProperties(server->bluezRxValueSnapshot(), server->bluezRxNotifyingSnapshot()));
        g_variant_builder_add(&objects, "{o@a{sa{sv}}}", kRxPath,
                              g_variant_builder_end(&interfaces));
    }

    return g_variant_builder_end(&objects);
}

void handleMethodCall(GDBusConnection* /*connection*/, const gchar* /*sender*/,
                      const gchar* object_path, const gchar* interface_name,
                      const gchar* method_name, GVariant* parameters,
                      GDBusMethodInvocation* invocation, gpointer user_data) {
    auto* server = static_cast<BleGattServer*>(user_data);
    std::string path = object_path ? object_path : "";
    std::string iface = interface_name ? interface_name : "";
    std::string method = method_name ? method_name : "";

    if (iface == "org.freedesktop.DBus.ObjectManager" && method == "GetManagedObjects") {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@a{oa{sa{sv}}})", buildManagedObjects(server)));
        return;
    }

    if (iface == "org.bluez.LEAdvertisement1" && method == "Release") {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (iface != "org.bluez.GattCharacteristic1") {
        g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                             "Unsupported interface");
        return;
    }

    if (method == "ReadValue") {
        std::string payload = server->bluezRxValueSnapshot();
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@ay)", bytesToVariant(payload)));
        return;
    }

    if (method == "WriteValue") {
        if (path != kTxPath) {
            g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                                 "Write is only supported on TX characteristic");
            return;
        }
        GVariant* bytes = nullptr;
        GVariant* options = nullptr;
        g_variant_get(parameters, "(@ay@a{sv})", &bytes, &options);
        std::string request = variantBytesToString(bytes);
        if (bytes) g_variant_unref(bytes);
        if (options) g_variant_unref(options);

        std::string response = server->handleCommand(request);
        server->notifyBluez(response);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (method == "StartNotify") {
        server->bluezSetNotifying(true);
        LOGI("BleGattServer", "Phone subscribed to RX notifications");
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (method == "StopNotify") {
        server->bluezSetNotifying(false);
        LOGI("BleGattServer", "Phone unsubscribed from RX notifications");
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                         "Unsupported method");
}

GVariant* getProperty(GDBusConnection* /*connection*/, const gchar* /*sender*/,
                      const gchar* object_path, const gchar* interface_name,
                      const gchar* property_name, GError** /*error*/, gpointer user_data) {
    auto* server = static_cast<BleGattServer*>(user_data);
    std::string path = object_path ? object_path : "";
    std::string iface = interface_name ? interface_name : "";
    std::string prop = property_name ? property_name : "";

    if (iface == "org.bluez.GattService1") {
        if (prop == "UUID") return g_variant_new_string(kServiceUuid);
        if (prop == "Primary") return g_variant_new_boolean(TRUE);
    }

    if (iface == "org.bluez.GattCharacteristic1") {
        if (path == kTxPath) {
            if (prop == "UUID") return g_variant_new_string(kTxUuid);
            if (prop == "Service") return g_variant_new_object_path(kServicePath);
            if (prop == "Flags") {
                GVariantBuilder arr;
                g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
                g_variant_builder_add(&arr, "s", "write");
                g_variant_builder_add(&arr, "s", "write-without-response");
                return g_variant_builder_end(&arr);
            }
        } else if (path == kRxPath) {
            if (prop == "UUID") return g_variant_new_string(kRxUuid);
            if (prop == "Service") return g_variant_new_object_path(kServicePath);
            if (prop == "Flags") {
                GVariantBuilder arr;
                g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
                g_variant_builder_add(&arr, "s", "read");
                g_variant_builder_add(&arr, "s", "notify");
                return g_variant_builder_end(&arr);
            }
            if (prop == "Value") return bytesToVariant(server->bluezRxValueSnapshot());
            if (prop == "Notifying") return g_variant_new_boolean(server->bluezRxNotifyingSnapshot() ? TRUE : FALSE);
        }
    }

    if (iface == "org.bluez.LEAdvertisement1") {
        if (prop == "Type") return g_variant_new_string("peripheral");
        if (prop == "LocalName") return g_variant_new_string("EdgeFocus");
        if (prop == "ServiceUUIDs") {
            GVariantBuilder arr;
            g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&arr, "s", kServiceUuid);
            return g_variant_builder_end(&arr);
        }
        if (prop == "Includes") {
            GVariantBuilder arr;
            g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
            g_variant_builder_add(&arr, "s", "tx-power");
            return g_variant_builder_end(&arr);
        }
    }

    return nullptr;
}

const GDBusInterfaceVTable kVTable = {
    handleMethodCall,
    getProperty,
    nullptr,
    {nullptr}
};

void onApplicationRegistered(GObject* source_object, GAsyncResult* result, gpointer user_data) {
    auto* server = static_cast<BleGattServer*>(user_data);
    server->bluezOnApplicationRegistered(source_object, result);
}

void onAdvertisementRegistered(GObject* source_object, GAsyncResult* result, gpointer user_data) {
    auto* server = static_cast<BleGattServer*>(user_data);
    server->bluezOnAdvertisementRegistered(source_object, result);
}
} // namespace

bool BleGattServer::initBluez() {
    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        if (bluez_app_registered_ || bluez_registering_app_) return bluez_ready_;
        if (bluez_next_retry_ms_ > 0 && monotonicMs() < bluez_next_retry_ms_) return false;
    }

    GError* error = nullptr;
    if (!bluez_connection_) {
        bluez_connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (!bluez_connection_) {
            LOGW("BleGattServer", "BlueZ bus unavailable: %s", error ? error->message : "unknown");
            if (error) g_error_free(error);
            std::lock_guard<std::mutex> lock(bluez_mutex_);
            bluez_next_retry_ms_ = monotonicMs() + 10000;
            return false;
        }
    }

    if (!object_manager_node_) object_manager_node_ = g_dbus_node_info_new_for_xml(kObjectManagerXml, &error);
    if (!service_node_) service_node_ = g_dbus_node_info_new_for_xml(kServiceXml, &error);
    if (!characteristic_node_) characteristic_node_ = g_dbus_node_info_new_for_xml(kCharacteristicXml, &error);
    if (!advertisement_node_) advertisement_node_ = g_dbus_node_info_new_for_xml(kAdvertisementXml, &error);
    if (!object_manager_node_ || !service_node_ || !characteristic_node_ || !advertisement_node_) {
        LOGW("BleGattServer", "Failed to parse BlueZ introspection XML: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        shutdownBluez();
        return false;
    }

    auto* connection = static_cast<GDBusConnection*>(bluez_connection_);
    auto* om_node = static_cast<GDBusNodeInfo*>(object_manager_node_);
    auto* service_node = static_cast<GDBusNodeInfo*>(service_node_);
    auto* char_node = static_cast<GDBusNodeInfo*>(characteristic_node_);
    auto* adv_node = static_cast<GDBusNodeInfo*>(advertisement_node_);

    if (!bluez_objects_registered_) {
        app_registration_id_ = g_dbus_connection_register_object(connection, kAppPath,
            om_node->interfaces[0], &kVTable, this, nullptr, &error);
        service_registration_id_ = g_dbus_connection_register_object(connection, kServicePath,
            service_node->interfaces[0], &kVTable, this, nullptr, &error);
        tx_registration_id_ = g_dbus_connection_register_object(connection, kTxPath,
            char_node->interfaces[0], &kVTable, this, nullptr, &error);
        rx_registration_id_ = g_dbus_connection_register_object(connection, kRxPath,
            char_node->interfaces[0], &kVTable, this, nullptr, &error);
        advertisement_registration_id_ = g_dbus_connection_register_object(connection, kAdvPath,
            adv_node->interfaces[0], &kVTable, this, nullptr, &error);

        if (!app_registration_id_ || !service_registration_id_ || !tx_registration_id_
            || !rx_registration_id_ || !advertisement_registration_id_) {
            LOGW("BleGattServer", "Failed to register local D-Bus objects: %s", error ? error->message : "unknown");
            if (error) g_error_free(error);
            shutdownBluez();
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(bluez_mutex_);
            bluez_objects_registered_ = true;
            if (rx_value_.empty()) {
                rx_value_ = AppCommandService::makeResponse(0, AppCommandService::json{{"ready", true}});
            }
        }
    }

    GVariantBuilder app_options;
    g_variant_builder_init(&app_options, G_VARIANT_TYPE("a{sv}"));
    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        bluez_registering_app_ = true;
    }
    g_dbus_connection_call(connection, "org.bluez", kAdapterPath,
        "org.bluez.GattManager1", "RegisterApplication",
        g_variant_new("(oa{sv})", kAppPath, &app_options), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, onApplicationRegistered, this);
    LOGI("BleGattServer", "BlueZ GATT registration requested");
    return false;
}

void BleGattServer::bluezOnApplicationRegistered(void* source_object, void* async_result) {
    auto* connection = G_DBUS_CONNECTION(source_object);
    auto* result = static_cast<GAsyncResult*>(async_result);
    GError* error = nullptr;
    GVariant* call_result = g_dbus_connection_call_finish(connection, result, &error);
    if (!call_result) {
        LOGW("BleGattServer", "RegisterApplication failed: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        shutdownBluez();
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        bluez_next_retry_ms_ = monotonicMs() + 10000;
        return;
    }
    g_variant_unref(call_result);

    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        bluez_registering_app_ = false;
        bluez_app_registered_ = true;
        bluez_ready_ = true;
        bluez_registering_advertisement_ = true;
    }

    GVariantBuilder adv_options;
    g_variant_builder_init(&adv_options, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(connection, "org.bluez", kAdapterPath,
        "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
        g_variant_new("(oa{sv})", kAdvPath, &adv_options), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, onAdvertisementRegistered, this);

    LOGI("BleGattServer", "BlueZ GATT registered: service=%s tx=%s rx=%s", kServiceUuid, kTxUuid, kRxUuid);
}

void BleGattServer::bluezOnAdvertisementRegistered(void* source_object, void* async_result) {
    auto* connection = G_DBUS_CONNECTION(source_object);
    auto* result = static_cast<GAsyncResult*>(async_result);
    GError* error = nullptr;
    GVariant* call_result = g_dbus_connection_call_finish(connection, result, &error);
    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        bluez_registering_advertisement_ = false;
        bluez_advertisement_registered_ = call_result != nullptr;
    }
    if (!call_result) {
        LOGW("BleGattServer", "RegisterAdvertisement failed: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        return;
    }
    g_variant_unref(call_result);
    LOGI("BleGattServer", "BlueZ advertisement registered: name=EdgeFocus");
}

std::string BleGattServer::bluezRxValueSnapshot() {
    std::lock_guard<std::mutex> lock(bluez_mutex_);
    return rx_value_;
}

bool BleGattServer::bluezRxNotifyingSnapshot() {
    std::lock_guard<std::mutex> lock(bluez_mutex_);
    return rx_notifying_;
}

void BleGattServer::bluezSetNotifying(bool notifying) {
    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        rx_notifying_ = notifying;
        phone_connected_.store(notifying);
    }
    event_outbox_.setSubscribed(notifying);
    if (!notifying) {
        PhoneUsageSignal sig;
        sig.active = false;
        sig.timestamp_ms = monotonicMs();
        store_.setPhoneSignal(sig);
        return;
    }
    auto response = nlohmann::json::parse(
        app_commands_.handleCommand(R"({"cmd":"get_current_state","id":0})"),
        nullptr,
        false);
    if (!response.is_discarded() && response.contains("data")) {
        notifyBluez(nlohmann::json{{"cmd", "push_state"}, {"data", response["data"]}}.dump());
    }
}

void BleGattServer::shutdownBluez() {
    auto* connection = static_cast<GDBusConnection*>(bluez_connection_);
    if (connection) {
        GVariantBuilder options;
        GError* error = nullptr;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        g_dbus_connection_call_sync(connection, "org.bluez", kAdapterPath,
            "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
            g_variant_new("(o)", kAdvPath), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
        if (error) g_error_free(error);
        error = nullptr;
        g_dbus_connection_call_sync(connection, "org.bluez", kAdapterPath,
            "org.bluez.GattManager1", "UnregisterApplication",
            g_variant_new("(o)", kAppPath), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
        if (error) g_error_free(error);

        if (advertisement_registration_id_) g_dbus_connection_unregister_object(connection, advertisement_registration_id_);
        if (rx_registration_id_) g_dbus_connection_unregister_object(connection, rx_registration_id_);
        if (tx_registration_id_) g_dbus_connection_unregister_object(connection, tx_registration_id_);
        if (service_registration_id_) g_dbus_connection_unregister_object(connection, service_registration_id_);
        if (app_registration_id_) g_dbus_connection_unregister_object(connection, app_registration_id_);
    }

    if (advertisement_node_) g_dbus_node_info_unref(static_cast<GDBusNodeInfo*>(advertisement_node_));
    if (characteristic_node_) g_dbus_node_info_unref(static_cast<GDBusNodeInfo*>(characteristic_node_));
    if (service_node_) g_dbus_node_info_unref(static_cast<GDBusNodeInfo*>(service_node_));
    if (object_manager_node_) g_dbus_node_info_unref(static_cast<GDBusNodeInfo*>(object_manager_node_));
    if (bluez_connection_) g_object_unref(static_cast<GDBusConnection*>(bluez_connection_));

    bluez_connection_ = nullptr;
    object_manager_node_ = nullptr;
    service_node_ = nullptr;
    characteristic_node_ = nullptr;
    advertisement_node_ = nullptr;
    app_registration_id_ = 0;
    service_registration_id_ = 0;
    tx_registration_id_ = 0;
    rx_registration_id_ = 0;
    advertisement_registration_id_ = 0;
    bluez_ready_ = false;
    bluez_objects_registered_ = false;
    bluez_registering_app_ = false;
    bluez_app_registered_ = false;
    bluez_registering_advertisement_ = false;
    bluez_advertisement_registered_ = false;
    bluez_next_retry_ms_ = 0;
    rx_notifying_ = false;
    phone_connected_.store(false);
    event_outbox_.setSubscribed(false);
    PhoneUsageSignal sig;
    sig.active = false;
    sig.timestamp_ms = monotonicMs();
    store_.setPhoneSignal(sig);
}

void BleGattServer::notifyBluez(const std::string& payload) {
    std::vector<std::string> payloads;
    if (payload.size() > kDirectPayloadBytes) {
        bool is_push = false;
        int request_id = 0;
        auto parsed = nlohmann::json::parse(payload, nullptr, false);
        if (!parsed.is_discarded()) {
            is_push = parsed.contains("cmd");
            request_id = parsed.value("id", 0);
        }
        payloads = makeChunkPayloadsForTest(payload, request_id, is_push, kChunkPayloadBytes);
    } else {
        payloads.push_back(payload);
    }

    for (const auto& outbound : payloads) {
    GDBusConnection* connection = nullptr;
    bool should_notify = false;
    {
        std::lock_guard<std::mutex> lock(bluez_mutex_);
        rx_value_ = outbound;
        connection = static_cast<GDBusConnection*>(bluez_connection_);
        should_notify = bluez_ready_ && rx_notifying_ && connection != nullptr;
        if (connection) g_object_ref(connection);
    }

    if (!should_notify) {
        if (connection) g_object_unref(connection);
        return;
    }

    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    addBytesProp(&changed, "Value", outbound);

    GVariantBuilder invalidated;
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));

    GError* error = nullptr;
    gboolean ok = g_dbus_connection_emit_signal(connection, nullptr, kRxPath,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1", &changed, &invalidated),
        &error);
    if (!ok) {
        LOGW("BleGattServer", "Failed to emit RX notification: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
    }
    g_object_unref(connection);
    if (payloads.size() > 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    }
}
#endif

} // namespace edgefocus
