#include "SettingsStore.h"

#include <Preferences.h>

namespace {
constexpr char kNamespace[] = "motomap";
constexpr char kWifiSsid[] = "wifi_ssid";
constexpr char kWifiPassword[] = "wifi_pass";
constexpr char kDestinationValid[] = "dest_valid";
constexpr char kDestinationActive[] = "dest_active";
constexpr char kDestinationLat[] = "dest_lat";
constexpr char kDestinationLon[] = "dest_lon";
constexpr char kDestinationLabel[] = "dest_label";
constexpr char kDestinationVehicle[] = "dest_vehicle";
}

bool SettingsStore::begin() {
    Preferences preferences;
    const bool ok = preferences.begin(kNamespace, false);
    preferences.end();
    return ok;
}

bool SettingsStore::loadWifi(String& ssid, String& password) const {
    Preferences preferences;
    if (!preferences.begin(kNamespace, true)) {
        return false;
    }
    ssid = preferences.getString(kWifiSsid, "");
    password = preferences.getString(kWifiPassword, "");
    preferences.end();
    return ssid.length() > 0;
}

bool SettingsStore::saveWifi(const String& ssid, const String& password) {
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) {
        return false;
    }
    const size_t ssidBytes = preferences.putString(kWifiSsid, ssid);
    const size_t passwordBytes = preferences.putString(kWifiPassword, password);
    preferences.end();
    return ssidBytes > 0 && (password.length() == 0 || passwordBytes > 0);
}

bool SettingsStore::hasWifi() const {
    String ssid;
    String password;
    return loadWifi(ssid, password);
}

DestinationData SettingsStore::loadDestination() const {
    DestinationData destination;
    Preferences preferences;
    if (!preferences.begin(kNamespace, true)) {
        return destination;
    }
    destination.valid = preferences.getBool(kDestinationValid, false);
    destination.active = preferences.getBool(kDestinationActive, false);
    destination.latitude = preferences.getDouble(kDestinationLat, 0.0);
    destination.longitude = preferences.getDouble(kDestinationLon, 0.0);
    destination.label = preferences.getString(kDestinationLabel, "");
    destination.vehicle = preferences.getString(kDestinationVehicle, "motorcycle");
    preferences.end();

    if (destination.latitude < -90.0 || destination.latitude > 90.0 ||
        destination.longitude < -180.0 || destination.longitude > 180.0) {
        destination.valid = false;
        destination.active = false;
    }
    return destination;
}

bool SettingsStore::saveDestination(const DestinationData& destination) {
    if (!destination.valid || destination.latitude < -90.0 || destination.latitude > 90.0 ||
        destination.longitude < -180.0 || destination.longitude > 180.0) {
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) {
        return false;
    }
    bool ok = preferences.putBool(kDestinationValid, true);
    ok = preferences.putBool(kDestinationActive, destination.active) && ok;
    ok = preferences.putDouble(kDestinationLat, destination.latitude) > 0 && ok;
    ok = preferences.putDouble(kDestinationLon, destination.longitude) > 0 && ok;
    ok = preferences.putString(kDestinationLabel, destination.label.substring(0, 80)) > 0 && ok;
    ok = preferences.putString(kDestinationVehicle, destination.vehicle) > 0 && ok;
    preferences.end();
    return ok;
}

bool SettingsStore::setDestinationActive(bool active) {
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) {
        return false;
    }
    const bool ok = preferences.putBool(kDestinationActive, active);
    preferences.end();
    return ok;
}
