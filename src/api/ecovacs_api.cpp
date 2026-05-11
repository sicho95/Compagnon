#include "ecovacs_api.h"
#include <WiFiClientSecure.h>
#include <vector>

EcovacsAPI::EcovacsAPI(const char* accessKey) : _accessKey(accessKey) {}

// ---- GET ----
bool EcovacsAPI::_get(const String& endpoint, JsonDocument& response) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, String(ECOVACS_OPEN_API) + endpoint);
    http.addHeader("Authorization", "Bearer " + _accessKey);
    http.addHeader("Content-Type", "application/json");
    int code = http.GET();
    if (code != 200) { Serial.printf("[Ecovacs] GET %s -> %d\n", endpoint.c_str(), code); http.end(); return false; }
    auto err = deserializeJson(response, http.getString());
    http.end();
    return err == DeserializationError::Ok;
}

// ---- POST ----
bool EcovacsAPI::_post(const String& endpoint, JsonDocument& body, JsonDocument& response) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, String(ECOVACS_OPEN_API) + endpoint);
    http.addHeader("Authorization", "Bearer " + _accessKey);
    http.addHeader("Content-Type", "application/json");
    String bodyStr; serializeJson(body, bodyStr);
    int code = http.POST(bodyStr);
    if (code != 200) { Serial.printf("[Ecovacs] POST %s -> %d\n", endpoint.c_str(), code); http.end(); return false; }
    auto err = deserializeJson(response, http.getString());
    http.end();
    return err == DeserializationError::Ok;
}

// ---- List robots ----
bool EcovacsAPI::listRobots(std::vector<EcovacsRobot>& robots) {
    JsonDocument doc;
    if (!_get("/getDeviceList", doc)) return false;
    if (doc["status"].as<int>() != 0) return false;
    robots.clear();
    for (JsonVariant v : doc["data"].as<JsonArray>()) {
        EcovacsRobot r;
        r.nickname = v.as<String>();
        robots.push_back(r);
    }
    Serial.printf("[Ecovacs] %d robot(s) trouvé(s)\n", (int)robots.size());
    return true;
}

// ---- Get state ----
bool EcovacsAPI::getState(const char* nickname, EcovacsState& state) {
    JsonDocument body;
    body["nickname"] = nickname;
    JsonDocument resp;
    if (!_post("/getCleanInfo", body, resp)) return false;
    if (resp["code"].as<int>() != 0) return false;

    JsonObject ctl = resp["data"]["ctl"]["data"];
    state.cleanSt   = ctl["cleanSt"].as<String>();
    state.chargeSt  = ctl["chargeSt"].as<String>();
    state.stationSt = ctl["stationSt"].as<String>();
    state.battery     = resp["data"]["battery"].as<int>();
    state.cleanedArea = resp["data"]["cleanedArea"].as<int>();
    state.cleanedTime = resp["data"]["cleanedTime"].as<int>();
    return true;
}

// ---- Clean ----
bool EcovacsAPI::clean(const char* nickname, const char* act) {
    JsonDocument body;
    body["nickname"] = nickname;
    body["act"]      = act;
    JsonDocument resp;
    if (!_post("/clean", body, resp)) return false;
    bool ok = resp["code"].as<int>() == 0;
    Serial.printf("[Ecovacs] clean(%s, %s) -> %s\n", nickname, act, ok ? "OK" : "FAIL");
    return ok;
}

// ---- Charge ----
bool EcovacsAPI::charge(const char* nickname, const char* act) {
    JsonDocument body;
    body["nickname"] = nickname;
    body["act"]      = act;
    JsonDocument resp;
    if (!_post("/charge", body, resp)) return false;
    bool ok = resp["code"].as<int>() == 0;
    Serial.printf("[Ecovacs] charge(%s, %s) -> %s\n", nickname, act, ok ? "OK" : "FAIL");
    return ok;
}

// ---- Helpers texte FR ----
const char* EcovacsAPI::cleanStFr(const String& st) {
    if (st=="s") return "Nettoyage";
    if (st=="p") return "Pause";
    if (st=="h") return "Inactif";
    if (st=="goposition") return "En route";
    if (st=="cruise") return "Croisiere";
    if (st=="buildmap") return "Cartographie";
    return "Inconnu";
}
const char* EcovacsAPI::chargeStFr(const String& st) {
    if (st=="g"||st=="gp") return "Retour base";
    if (st=="charging"||st=="sc"||st=="wc") return "En charge";
    if (st=="i") return "Libre";
    return "Inconnu";
}
const char* EcovacsAPI::stationStFr(const String& st) {
    if (st=="i")    return "Base OK";
    if (st=="wash") return "Lavage raclette";
    if (st=="dry")  return "Sechage";
    if (st=="dust") return "Collecte poussiere";
    if (st=="clean")return "Nettoyage base";
    return st.c_str();
}
