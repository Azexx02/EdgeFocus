#include "BleBandController.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#ifdef __linux__
#include <signal.h>
#endif

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
#include <gio/gio.h>
#endif

namespace edgefocus {

namespace {
constexpr const char* kAdapterPath = "/org/bluez/hci0";
constexpr const char* kBluez = "org.bluez";
constexpr const char* kObjectManager = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kDeviceInterface = "org.bluez.Device1";
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kGattServiceInterface = "org.bluez.GattService1";
constexpr const char* kGattCharInterface = "org.bluez.GattCharacteristic1";
constexpr const char* kBandServiceUuid = "0000180c-0000-1000-8000-00805f9b34fb";
constexpr const char* kBandCharUuid = "00002a56-0000-1000-8000-00805f9b34fb";

std::string envOrDefault(const char* name, const char* fallback) {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr || value[0] == '\0') {
        free(value);
        return fallback;
    }
    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') return fallback;
    return value;
#endif
}

uint8_t patternForLevel(uint8_t level) {
    if (level >= 3) return 0x03;
    if (level >= 2) return 0x02;
    return 0x01;
}

uint8_t clampBandLevel(uint8_t level) {
    if (level < 1) return 1;
    if (level > 3) return 3;
    return level;
}

#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
struct BluezPaths {
    std::string device;
    std::string service;
    std::string characteristic;
};

std::string macToDevicePath(const std::string& mac) {
    std::string path = std::string(kAdapterPath) + "/dev_";
    for (char c : mac) path.push_back(c == ':' ? '_' : c);
    return path;
}

std::string variantString(GVariant* interfaces, const char* iface, const char* prop) {
    GVariant* iface_props = g_variant_lookup_value(interfaces, iface, G_VARIANT_TYPE("a{sv}"));
    if (!iface_props) return "";
    GVariant* value = g_variant_lookup_value(iface_props, prop, nullptr);
    g_variant_unref(iface_props);
    if (!value) return "";
    const char* str = g_variant_get_string(value, nullptr);
    std::string out = str ? str : "";
    g_variant_unref(value);
    return out;
}

bool variantBool(GVariant* interfaces, const char* iface, const char* prop, bool fallback = false) {
    GVariant* iface_props = g_variant_lookup_value(interfaces, iface, G_VARIANT_TYPE("a{sv}"));
    if (!iface_props) return fallback;
    GVariant* value = g_variant_lookup_value(iface_props, prop, nullptr);
    g_variant_unref(iface_props);
    if (!value) return fallback;
    bool out = g_variant_get_boolean(value);
    g_variant_unref(value);
    return out;
}

bool setAdapterPowered(GDBusConnection* bus) {
    GError* error = nullptr;
    GVariant* params = g_variant_new("(ssv)", kAdapterInterface, "Powered", g_variant_new_boolean(TRUE));
    GVariant* result = g_dbus_connection_call_sync(bus, kBluez, kAdapterPath, kProperties,
                                                   "Set", params, nullptr,
                                                   G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (!result) {
        LOGW("BleBandController", "Adapter power on failed: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);
    return true;
}

bool callAdapterMethod(GDBusConnection* bus, const char* method, int timeout_ms) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(bus, kBluez, kAdapterPath, kAdapterInterface,
                                                   method, nullptr, nullptr,
                                                   G_DBUS_CALL_FLAGS_NONE, timeout_ms, nullptr, &error);
    if (!result) {
        LOGW("BleBandController", "Adapter %s failed: %s", method, error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);
    return true;
}

bool callDeviceMethod(GDBusConnection* bus, const std::string& path, const char* method, int timeout_ms) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(bus, kBluez, path.c_str(), kDeviceInterface,
                                                   method, nullptr, nullptr,
                                                   G_DBUS_CALL_FLAGS_NONE, timeout_ms, nullptr, &error);
    if (!result) {
        LOGW("BleBandController", "Device %s %s failed: %s", path.c_str(), method,
             error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);
    return true;
}

bool getManagedObjects(GDBusConnection* bus, GVariant** objects_out) {
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(bus, kBluez, "/", kObjectManager,
                                                   "GetManagedObjects", nullptr,
                                                   G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                                   G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (!result) {
        LOGW("BleBandController", "GetManagedObjects failed: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }
    g_variant_get(result, "(@a{oa{sa{sv}}})", objects_out);
    g_variant_unref(result);
    return true;
}

BluezPaths findBandPaths(GDBusConnection* bus, const std::string& mac, const std::string& name) {
    BluezPaths paths;
    GVariant* objects = nullptr;
    if (!getManagedObjects(bus, &objects)) return paths;

    std::string expected_device = mac.empty() ? "" : macToDevicePath(mac);
    GVariantIter iter;
    const char* object_path = nullptr;
    GVariant* interfaces = nullptr;

    g_variant_iter_init(&iter, objects);
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariant* device_props = g_variant_lookup_value(interfaces, kDeviceInterface, G_VARIANT_TYPE("a{sv}"));
        if (device_props) {
            g_variant_unref(device_props);
            const bool mac_match = !expected_device.empty() && expected_device == object_path;
            std::string alias = variantString(interfaces, kDeviceInterface, "Alias");
            std::string dev_name = variantString(interfaces, kDeviceInterface, "Name");
            const bool name_match = !name.empty() && (alias == name || dev_name == name);
            if (mac_match || (expected_device.empty() && name_match)) {
                paths.device = object_path;
            }
        }
        g_variant_unref(interfaces);
    }

    if (paths.device.empty()) {
        g_variant_unref(objects);
        return paths;
    }

    g_variant_iter_init(&iter, objects);
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        std::string object(object_path);
        if (object.rfind(paths.device + "/", 0) == 0) {
            std::string service_uuid = variantString(interfaces, kGattServiceInterface, "UUID");
            if (service_uuid == kBandServiceUuid) {
                paths.service = object;
            }
        }
        g_variant_unref(interfaces);
    }

    if (!paths.service.empty()) {
        g_variant_iter_init(&iter, objects);
        while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
            std::string object(object_path);
            if (object.rfind(paths.service + "/", 0) == 0) {
                std::string char_uuid = variantString(interfaces, kGattCharInterface, "UUID");
                if (char_uuid == kBandCharUuid) {
                    paths.characteristic = object;
                }
            }
            g_variant_unref(interfaces);
        }
    }
    g_variant_unref(objects);
    return paths;
}

bool waitForServicesResolved(GDBusConnection* bus, const std::string& device_path,
                             std::atomic<bool>& stop_requested) {
    for (int i = 0; i < 30 && !stop_requested.load(); ++i) {
        GVariant* objects = nullptr;
        if (getManagedObjects(bus, &objects)) {
            GVariantIter iter;
            const char* object_path = nullptr;
            GVariant* interfaces = nullptr;
            bool resolved = false;
            g_variant_iter_init(&iter, objects);
            while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
                if (device_path == object_path) {
                    resolved = variantBool(interfaces, kDeviceInterface, "ServicesResolved", false);
                }
                g_variant_unref(interfaces);
            }
            g_variant_unref(objects);
            if (resolved) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}
#endif
} // namespace

BleBandController::BleBandController() = default;
BleBandController::~BleBandController() { stopConnection(); }

std::array<uint8_t, 3> BleBandController::lastAlertFrame() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_alert_frame_;
}

std::string BleBandController::lastWriteStatus() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_write_status_;
}

void BleBandController::setWriteStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_write_status_ = status;
}

void BleBandController::startConnection() {
    if (enabled_.load()) return;
    if (conn_thread_.joinable() || writer_thread_.joinable()) stopConnection();
    stop_requested_ = false;
    enabled_ = true;
    writer_thread_ = std::thread(&BleBandController::writerThreadFunc, this);
#ifdef __linux__
    conn_thread_ = std::thread(&BleBandController::connectionThreadFunc, this);
#endif
    LOGI("BleBandController", "BLE connection thread started");
}

void BleBandController::stopConnection() {
    if (!enabled_.load() && !conn_thread_.joinable() && !writer_thread_.joinable()) return;
    stop_requested_ = true;
    enabled_ = false;
    connected_ = false;
    writer_cv_.notify_all();
    if (conn_thread_.joinable()) conn_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_alert_.reset();
    }
#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    if (bluez_bus_) {
        g_object_unref(static_cast<GDBusConnection*>(bluez_bus_));
        bluez_bus_ = nullptr;
    }
#endif
    LOGI("BleBandController", "BLE connection stopped");
}

bool BleBandController::sendAlert(uint8_t level) {
    const uint8_t clamped_level = clampBandLevel(level);
    uint8_t pattern = patternForLevel(clamped_level);
    last_alert_pattern_ = pattern;
    last_alert_level_ = clamped_level;
    bool accepted = true;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_alert_frame_ = {{0x01, pattern, clamped_level}};
        if (pending_alert_ && pending_alert_->level > clamped_level) {
            accepted = false;
            last_write_status_ = "kept higher-priority pending alert";
        } else {
            QueuedAlert alert;
            alert.sequence = ++alert_sequence_;
            alert.pattern = pattern;
            alert.level = clamped_level;
            alert.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            pending_alert_ = alert;
            last_write_status_ = connected_.load() ? "queued" : "queued: waiting for connection";
        }
    }
    if (accepted) writer_cv_.notify_one();
    LOGI("BleBandController", "BLE alert %s (level=%d, pattern=0x%02x)",
         accepted ? "queued" : "coalesced", clamped_level, pattern);
    return accepted;
}

void BleBandController::writerThreadFunc() {
    while (!stop_requested_.load()) {
        QueuedAlert alert;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            writer_cv_.wait_for(lock, std::chrono::milliseconds(250), [&] {
                return stop_requested_.load() || (pending_alert_.has_value() && connected_.load());
            });
            if (stop_requested_.load()) break;
            if (!pending_alert_) continue;
            const auto now = std::chrono::steady_clock::now();
            if (now >= pending_alert_->expires_at) {
                last_write_status_ = "expired: alert ttl elapsed";
                pending_alert_.reset();
                continue;
            }
            if (!connected_.load()) continue;
            alert = *pending_alert_;
            pending_alert_.reset();
            last_write_status_ = "sending";
        }

        const bool ok = writeAlertFrame(alert.pattern, alert.level);
        if (ok) {
            sent_sequence_.store(alert.sequence);
            LOGI("BleBandController", "BLE alert sent (level=%d, pattern=0x%02x)",
                 alert.level, alert.pattern);
            continue;
        }

        if (!connected_.load()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!pending_alert_ || alert.level >= pending_alert_->level) {
                pending_alert_ = alert;
                last_write_status_ = "queued: reconnect retry";
            }
            continue;
        }

        ++alert.failure_count;
        const auto now = std::chrono::steady_clock::now();
        if (now >= alert.expires_at || alert.failure_count >= 3) {
            setWriteStatus(now >= alert.expires_at
                ? "expired: alert ttl elapsed"
                : "failed: retry budget exhausted");
            continue;
        }

        const auto retry_delay = alert.failure_count == 1
            ? std::chrono::milliseconds(500)
            : std::chrono::milliseconds(1500);
        std::unique_lock<std::mutex> lock(state_mutex_);
        writer_cv_.wait_for(lock, retry_delay, [&] { return stop_requested_.load(); });
        if (stop_requested_.load()) break;
        if (!pending_alert_ || alert.level >= pending_alert_->level) {
            pending_alert_ = alert;
            last_write_status_ = connected_.load() ? "queued: retry" : "queued: reconnect retry";
        }
    }
}

bool BleBandController::writeAlertFrame(uint8_t pattern, uint8_t level) {
    write_attempt_count_++;
#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    GDBusConnection* bus = static_cast<GDBusConnection*>(bluez_bus_);
    if (!bus) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_write_status_ = "failed: no system bus";
        return false;
    }

    std::string mac = envOrDefault("EDGEFOCUS_BAND_MAC", "");
    std::string name = envOrDefault("EDGEFOCUS_BAND_NAME", "EdgefocusBand");
    BluezPaths paths = findBandPaths(bus, mac, name);
    if (paths.characteristic.empty()) {
        connected_ = false;
        writer_cv_.notify_one();
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_write_status_ = "failed: 0x2A56 characteristic not found";
        return false;
    }

    GVariantBuilder bytes;
    g_variant_builder_init(&bytes, G_VARIANT_TYPE("ay"));
    g_variant_builder_add(&bytes, "y", static_cast<guchar>(0x01));
    g_variant_builder_add(&bytes, "y", static_cast<guchar>(pattern));
    g_variant_builder_add(&bytes, "y", static_cast<guchar>(level));

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    GVariant* params = g_variant_new("(aya{sv})", &bytes, &options);

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(bus, kBluez, paths.characteristic.c_str(),
                                                   kGattCharInterface, "WriteValue", params, nullptr,
                                                   G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!result) {
        last_write_status_ = std::string("failed: ") + (error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }
    g_variant_unref(result);
    last_write_status_ = "ok: " + paths.characteristic;
    return true;
#else
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_write_status_ = "diagnostic-only backend";
    return connected_.load();
#endif
}

#ifdef __linux__
void BleBandController::connectionThreadFunc() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

#if defined(EDGEFOCUS_WITH_BLUEZ)
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        LOGE("BleBandController", "Failed to open BlueZ system bus: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        enabled_ = false;
        return;
    }
    bluez_bus_ = bus;

    while (!stop_requested_.load()) {
        std::string target_name = envOrDefault("EDGEFOCUS_BAND_NAME", "EdgefocusBand");
        std::string mac = envOrDefault("EDGEFOCUS_BAND_MAC", "");
        LOGI("BleBandController", "Connecting BLE band via BlueZ (name=%s mac=%s)",
             target_name.c_str(), mac.empty() ? "<scan>" : mac.c_str());

        setAdapterPowered(bus);
        callAdapterMethod(bus, "StartDiscovery", 3000);

        BluezPaths paths;
        for (int i = 0; i < 24 && !stop_requested_.load(); ++i) {
            paths = findBandPaths(bus, mac, target_name);
            if (!paths.device.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        callAdapterMethod(bus, "StopDiscovery", 3000);

        if (paths.device.empty()) {
            LOGW("BleBandController", "BLE band not found, retrying");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        if (!callDeviceMethod(bus, paths.device, "Connect", 15000)) {
            connected_ = false;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        if (!waitForServicesResolved(bus, paths.device, stop_requested_)) {
            connected_ = false;
            LOGW("BleBandController", "BLE services not resolved for %s", paths.device.c_str());
            callDeviceMethod(bus, paths.device, "Disconnect", 5000);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        paths = findBandPaths(bus, mac, target_name);
        if (paths.characteristic.empty()) {
            connected_ = false;
            LOGW("BleBandController", "BLE 0x2A56 characteristic not found after connect");
            callDeviceMethod(bus, paths.device, "Disconnect", 5000);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        connected_ = true;
        writer_cv_.notify_one();
        LOGI("BleBandController", "Connected to BLE band target (%s), char=%s",
             paths.device.c_str(), paths.characteristic.c_str());

        while (!stop_requested_.load() && connected_.load()) {
            BluezPaths current = findBandPaths(bus, mac, target_name);
            if (current.device.empty() ||
                !waitForServicesResolved(bus, current.device, stop_requested_)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        connected_ = false;
        writer_cv_.notify_one();
        callDeviceMethod(bus, paths.device, "Disconnect", 5000);
        if (!stop_requested_.load()) {
            LOGW("BleBandController", "BLE band connection lost, reconnecting");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
#else
    while (!stop_requested_.load()) {
        connected_ = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif
    connected_ = false;
}
#endif

} // namespace edgefocus
