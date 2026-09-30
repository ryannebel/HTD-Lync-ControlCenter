#include "LyncSerial.h"
#include "ControlPage.h"
#include "NamesPage.h"
#include "EthernetPage.h"
#include "Rs232Page.h"

#include <Arduino.h>
#include <ESP.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ETH.h>
#include <SPI.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

#include <WebSocketsServer.h>
#define LYNC_HAS_WEBSOCKET 1


const char* OTA_HOSTNAME = "lync-controlcenter";
const char* WIFI_PORTAL_SSID = "Lync-Control-Setup";

static const uint8_t LYNC_MAX_ZONES = 12;
static const uint8_t LYNC_MAX_SOURCES = 18;
static const uint32_t LYNC_ALL_SOURCES_MASK = (1UL << LYNC_MAX_SOURCES) - 1UL;
static const int LYNC_UART_RX_PIN = 16;
static const int LYNC_UART_TX_PIN = 17;
static const uint32_t LYNC_BAUD = 38400;
static const uint8_t ETH_PHY_ADDR = 1;
static const int ETH_SPI_CS_PIN = 14;
static const int ETH_SPI_IRQ_PIN = 10;
static const int ETH_SPI_RST_PIN = 9;
static const int ETH_SPI_SCK_PIN = 13;
static const int ETH_SPI_MISO_PIN = 12;
static const int ETH_SPI_MOSI_PIN = 11;

struct EthernetConfig {
  bool dhcp = true;
  IPAddress ip = IPAddress(192, 168, 1, 50);
  IPAddress gateway = IPAddress(192, 168, 1, 1);
  IPAddress subnet = IPAddress(255, 255, 255, 0);
  IPAddress dns1 = IPAddress(8, 8, 8, 8);
  IPAddress dns2 = IPAddress(1, 1, 1, 1);
};

HardwareSerial LyncUart(2);
LyncSerial lync(LyncUart, LYNC_MAX_ZONES);

WebServer server(80);
#if LYNC_HAS_WEBSOCKET
WebSocketsServer ws(81);
#endif
Preferences prefs;
EthernetConfig ethernetConfig;
bool ethHasIp = false;
bool wifiHasIp = false;
bool ethStarted = false;

struct ZoneCache {
  bool known = false;
  bool powerOn = false;
  bool muteOn = false;
  bool dndOn = false;
  uint8_t input = 0;
  int8_t volumeDb = 0;
  int8_t trebleDb = 0;
  int8_t bassDb = 0;
  int8_t balanceDb = 0;
};

struct ZoneFieldHold {
  uint32_t inputUntilMs = 0;
  uint32_t volumeUntilMs = 0;
  uint32_t trebleUntilMs = 0;
  uint32_t bassUntilMs = 0;
};

static const uint32_t kStateHoldMs = 8000;

ZoneCache zoneState[LYNC_MAX_ZONES + 1];
ZoneFieldHold zoneHold[LYNC_MAX_ZONES + 1];
String zoneFullName[LYNC_MAX_ZONES + 1];
String zoneShortName[LYNC_MAX_ZONES + 1];
String sourceFullName[LYNC_MAX_SOURCES + 1];
String sourceShortName[LYNC_MAX_SOURCES + 1];
uint32_t zoneSourceVisibilityMask[LYNC_MAX_ZONES + 1];

bool rs232Paused = false;
String rs232Log;
static const size_t kMaxRs232LogChars = 12000;

bool holdActive(uint32_t untilMs) {
  return static_cast<int32_t>(untilMs - millis()) > 0;
}

void appendRs232Log(const String& line);
String rs232BytesToHex(const uint8_t* data, size_t len);

void capturePausedRs232Rx() {
  uint8_t buffer[64] = {0};
  size_t count = 0;
  while (LyncUart.available() > 0 && count < sizeof(buffer)) {
	const int c = LyncUart.read();
	if (c < 0) {
	  break;
	}
	buffer[count++] = static_cast<uint8_t>(c);
  }

  if (count > 0) {
	appendRs232Log("[RX] " + rs232BytesToHex(buffer, count));
  }
}

String jsonEscape(const String& value);
bool parseZoneArg(uint8_t &zone);
bool parseSourceArg(uint8_t &source);
bool parseSourceMaskArg(uint32_t &mask);
bool parseIpArg(const String& name, IPAddress& out);
void sendJson(bool ok, const String &message);
void saveZoneNames(uint8_t zone);
void saveSourceNames(uint8_t source);
void saveZoneSourceVisibility(uint8_t zone);
void saveEthernetConfig();
void loadEthernetConfig();
bool connectEthernet(uint32_t timeoutMs);
bool connectWifi(uint32_t timeoutMs);
bool startWifiProvisioningPortal(uint32_t timeoutSec);
bool hasSavedWifiCredentials();
void connectNetwork();
void onNetworkEvent(WiFiEvent_t event);
void appendRs232Log(const String& line);
String rs232BytesToHex(const uint8_t* data, size_t len);
bool parseHexCommandString(const String& input, uint8_t* out, size_t& outLen, size_t maxLen, String& error);

uint32_t clampSourceMask(uint32_t mask) {
  return mask & LYNC_ALL_SOURCES_MASK;
}

String shortName10(const String& value) {
  if (value.length() <= 10) {
	return value;
  }
  return value.substring(0, 10);
}

void appendRs232Log(const String& line) {
  const String entry = line + "\n";
  rs232Log += entry;
  if (rs232Log.length() > kMaxRs232LogChars) {
	rs232Log.remove(0, rs232Log.length() - kMaxRs232LogChars);
  }
}

String rs232BytesToHex(const uint8_t* data, size_t len) {
  String out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; ++i) {
	char b[4] = {0};
	snprintf(b, sizeof(b), "%02X", data[i]);
	if (i > 0) {
	  out += ' ';
	}
	out += b;
  }
  return out;
}

bool parseHexCommandString(const String& input, uint8_t* out, size_t& outLen, size_t maxLen, String& error) {
  outLen = 0;
  String token;

  auto flushToken = [&]() -> bool {
	if (token.length() == 0) {
	  return true;
	}

	String t = token;
	t.trim();
	token = "";
	if (t.length() == 0) {
	  return true;
	}

	if (t.startsWith("0x") || t.startsWith("0X")) {
	  t = t.substring(2);
	}

	if (t.length() == 0 || t.length() > 2) {
	  error = "Invalid byte token: " + t;
	  return false;
	}

	char* endPtr = nullptr;
	const long v = strtol(t.c_str(), &endPtr, 16);
	if (endPtr == t.c_str() || *endPtr != '\0' || v < 0 || v > 255) {
	  error = "Invalid hex byte: " + t;
	  return false;
	}

	if (outLen >= maxLen) {
	  error = "Too many bytes";
	  return false;
	}

	out[outLen++] = static_cast<uint8_t>(v);
	return true;
  };

  for (size_t i = 0; i < input.length(); ++i) {
	const char c = input[i];
	if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t') {
	  if (!flushToken()) {
		return false;
	  }
	  continue;
	}
	token += c;
  }

  if (!flushToken()) {
	return false;
  }

  if (outLen == 0) {
	error = "No bytes provided";
	return false;
  }

  return true;
}

bool parseSourceArg(uint8_t &source) {
  if (!server.hasArg("source")) {
	return false;
  }
  int v = server.arg("source").toInt();
  if (v < 1 || v > LYNC_MAX_SOURCES) {
	return false;
  }
  source = static_cast<uint8_t>(v);
  return true;
}

bool parseSourceMaskArg(uint32_t &mask) {
  if (!server.hasArg("mask")) {
	return false;
  }

  const String raw = server.arg("mask");
  char* endPtr = nullptr;
  const unsigned long parsed = strtoul(raw.c_str(), &endPtr, 10);
  if (endPtr == raw.c_str() || *endPtr != '\0') {
	return false;
  }

  mask = clampSourceMask(static_cast<uint32_t>(parsed));
  return true;
}

void handleApiNames() {
  String body = "{\"ok\":true,\"zones\":[";
  for (uint8_t zone = 1; zone <= LYNC_MAX_ZONES; ++zone) {
	if (zone > 1) {
	  body += ",";
	}
	body += "{\"zone\":" + String(zone);
	body += ",\"full\":\"" + jsonEscape(zoneFullName[zone]) + "\"";
	body += ",\"short\":\"" + jsonEscape(zoneShortName[zone]) + "\"";
	body += ",\"sourceMask\":" + String(zoneSourceVisibilityMask[zone]) + "}";
  }
  body += "],\"sources\":[";
  for (uint8_t source = 1; source <= LYNC_MAX_SOURCES; ++source) {
	if (source > 1) {
	  body += ",";
	}
	body += "{\"source\":" + String(source);
	body += ",\"full\":\"" + jsonEscape(sourceFullName[source]) + "\"";
	body += ",\"short\":\"" + jsonEscape(sourceShortName[source]) + "\"}";
  }
  body += "]}";
  server.send(200, "application/json", body);
}

void handleApiWifiClear() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFiManager wm;
  wm.resetSettings();

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"wifi credentials cleared; restarting\"}");
  delay(250);
  ESP.restart();
}

void handleApiZoneNameByIndex() {
  uint8_t zone = 0;
  if (!parseZoneArg(zone)) {
	sendJson(false, "zone required");
	return;
  }

  String body = "{\"ok\":true,\"zone\":" + String(zone);
  body += ",\"full\":\"" + jsonEscape(zoneFullName[zone]) + "\"}";
  server.send(200, "application/json", body);
}

void handleApiSourceNameByIndex() {
  uint8_t source = 0;
  if (!parseSourceArg(source)) {
	sendJson(false, "source required");
	return;
  }

  String body = "{\"ok\":true,\"source\":" + String(source);
  body += ",\"full\":\"" + jsonEscape(sourceFullName[source]) + "\"}";
  server.send(200, "application/json", body);
}

void handleApiSetZoneName() {
  uint8_t zone = 0;
  if (!parseZoneArg(zone) || !server.hasArg("full") || !server.hasArg("short")) {
	sendJson(false, "zone, full and short required");
	return;
  }

  const String full = server.arg("full");
  const String shortValue = shortName10(server.arg("short"));
  const bool shortChanged = shortValue != zoneShortName[zone];

  zoneFullName[zone] = full;
  zoneShortName[zone] = shortValue;
  saveZoneNames(zone);

  if (shortChanged) {
	if (!lync.setZoneName(zone, zoneShortName[zone].c_str())) {
	  sendJson(false, "saved but failed to write Lync zone short name");
	  return;
	}
  }

  sendJson(true, "ok");
}

void handleApiSetZoneSourceMask() {
  uint8_t zone = 0;
  uint32_t mask = 0;
  if (!parseZoneArg(zone) || !parseSourceMaskArg(mask)) {
	sendJson(false, "zone and mask required");
	return;
  }

  zoneSourceVisibilityMask[zone] = mask;
  saveZoneSourceVisibility(zone);
  sendJson(true, "ok");
}

void handleApiSetSourceName() {
  uint8_t source = 0;
  if (!parseSourceArg(source) || !server.hasArg("full") || !server.hasArg("short")) {
	sendJson(false, "source, full and short required");
	return;
  }

  const String full = server.arg("full");
  const String shortValue = shortName10(server.arg("short"));
  const bool shortChanged = shortValue != sourceShortName[source];

  sourceFullName[source] = full;
  sourceShortName[source] = shortValue;
  saveSourceNames(source);

  if (shortChanged) {
	bool ok = true;
	for (uint8_t zone = 1; zone <= LYNC_MAX_ZONES; ++zone) {
	  if (!lync.setSourceName(zone, source, sourceShortName[source].c_str())) {
		ok = false;
	  }
	  delay(8);
	}
	if (!ok) {
	  sendJson(false, "saved but failed to write one or more Lync source short names");
	  return;
	}
  }

  sendJson(true, "ok");
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
	const char c = value[i];
	switch (c) {
	case '\\': out += "\\\\"; break;
	case '"': out += "\\\""; break;
	case '\n': out += "\\n"; break;
	case '\r': out += "\\r"; break;
	case '\t': out += "\\t"; break;
	default: out += c; break;
	}
  }
  return out;
}

void prefsKey(char* out, size_t outLen, const char* prefix, uint8_t index) {
  snprintf(out, outLen, "%s%u", prefix, static_cast<unsigned>(index));
}

void saveZoneNames(uint8_t zone) {
  char key[8] = {0};
  prefsKey(key, sizeof(key), "zf", zone);
  prefs.putString(key, zoneFullName[zone]);
  prefsKey(key, sizeof(key), "zs", zone);
  prefs.putString(key, zoneShortName[zone]);
}

void saveSourceNames(uint8_t source) {
  char key[8] = {0};
  prefsKey(key, sizeof(key), "sf", source);
  prefs.putString(key, sourceFullName[source]);
  prefsKey(key, sizeof(key), "ss", source);
  prefs.putString(key, sourceShortName[source]);
}

void saveZoneSourceVisibility(uint8_t zone) {
  char key[8] = {0};
  prefsKey(key, sizeof(key), "zm", zone);
  prefs.putULong(key, static_cast<unsigned long>(clampSourceMask(zoneSourceVisibilityMask[zone])));
}

void loadNamesFromPrefs() {
  prefs.begin("lync-names", false);
  loadEthernetConfig();

  for (uint8_t zone = 1; zone <= LYNC_MAX_ZONES; ++zone) {
	char key[8] = {0};
	const String defaultZone = String("Zone ") + zone;

	prefsKey(key, sizeof(key), "zf", zone);
	zoneFullName[zone] = prefs.getString(key, defaultZone);

	prefsKey(key, sizeof(key), "zs", zone);
	zoneShortName[zone] = shortName10(prefs.getString(key, defaultZone));

	prefsKey(key, sizeof(key), "zm", zone);
	zoneSourceVisibilityMask[zone] = clampSourceMask(static_cast<uint32_t>(prefs.getULong(key, static_cast<unsigned long>(LYNC_ALL_SOURCES_MASK))));
  }

  for (uint8_t source = 1; source <= LYNC_MAX_SOURCES; ++source) {
	char key[8] = {0};
	const String defaultSource = String("Input ") + source;

	prefsKey(key, sizeof(key), "sf", source);
	sourceFullName[source] = prefs.getString(key, defaultSource);

	prefsKey(key, sizeof(key), "ss", source);
	sourceShortName[source] = shortName10(prefs.getString(key, defaultSource));
  }
}

String zoneJson(uint8_t z) {
  String body = "{\"zone\":" + String(z);
  body += ",\"known\":" + String(zoneState[z].known ? "true" : "false");
  body += ",\"powerOn\":" + String(zoneState[z].powerOn ? "true" : "false");
  body += ",\"muteOn\":" + String(zoneState[z].muteOn ? "true" : "false");
  body += ",\"dndOn\":" + String(zoneState[z].dndOn ? "true" : "false");
  body += ",\"input\":" + String(zoneState[z].input);
  body += ",\"volumeDb\":" + String(zoneState[z].volumeDb);
  body += ",\"trebleDb\":" + String(zoneState[z].trebleDb);
  body += ",\"bassDb\":" + String(zoneState[z].bassDb);
  body += ",\"balanceDb\":" + String(zoneState[z].balanceDb);
  body += "}";
  return body;
}

void broadcastZone(uint8_t zone) {
  if (zone < 1 || zone > LYNC_MAX_ZONES) {
	return;
  }
#if LYNC_HAS_WEBSOCKET
  String payload = "{\"type\":\"zone\",\"data\":";
  payload += zoneJson(zone);
  payload += "}";
  ws.broadcastTXT(payload);
#endif
}

void broadcastInfo(const String &message) {
#if LYNC_HAS_WEBSOCKET
  String payload = "{\"type\":\"info\",\"message\":\"" + message + "\"}";
  ws.broadcastTXT(payload);
#else
  (void)message;
#endif
}

bool parseZoneArg(uint8_t &zone) {
  if (!server.hasArg("zone")) {
	return false;
  }
  int v = server.arg("zone").toInt();
  if (v < 1 || v > LYNC_MAX_ZONES) {
	return false;
  }
  zone = static_cast<uint8_t>(v);
  return true;
}

bool parseIntArg(const char *name, int &value) {
  if (!server.hasArg(name)) {
	return false;
  }
  value = server.arg(name).toInt();
  return true;
}

bool parseIpArg(const String& name, IPAddress& out) {
  if (!server.hasArg(name)) {
	return false;
  }
  return out.fromString(server.arg(name));
}

String ipToString(const IPAddress& ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

void saveEthernetConfig() {
  prefs.putBool("edh", ethernetConfig.dhcp);
  prefs.putString("eip", ipToString(ethernetConfig.ip));
  prefs.putString("egw", ipToString(ethernetConfig.gateway));
  prefs.putString("esn", ipToString(ethernetConfig.subnet));
  prefs.putString("ed1", ipToString(ethernetConfig.dns1));
  prefs.putString("ed2", ipToString(ethernetConfig.dns2));
}

void loadEthernetConfig() {
  ethernetConfig.dhcp = prefs.getBool("edh", true);
  ethernetConfig.ip.fromString(prefs.getString("eip", "192.168.1.50"));
  ethernetConfig.gateway.fromString(prefs.getString("egw", "192.168.1.1"));
  ethernetConfig.subnet.fromString(prefs.getString("esn", "255.255.255.0"));
  ethernetConfig.dns1.fromString(prefs.getString("ed1", "8.8.8.8"));
  ethernetConfig.dns2.fromString(prefs.getString("ed2", "1.1.1.1"));
}

void sendJson(bool ok, const String &message) {
  String body = "{\"ok\":";
  body += ok ? "true" : "false";
  body += ",\"message\":\"";
  body += message;
  body += "\"}";
  server.send(ok ? 200 : 400, "application/json", body);
}

uint8_t commandChecksum(uint8_t zone, uint8_t command, uint8_t data) {
  return static_cast<uint8_t>(0x02 + 0x00 + zone + command + data);
}

String commandFrameHex(uint8_t zone, uint8_t command, uint8_t data) {
  char buffer[32] = {0};
  const uint8_t checksum = commandChecksum(zone, command, data);
  snprintf(buffer, sizeof(buffer), "02 00 %02X %02X %02X %02X", zone, command, data, checksum);
  return String(buffer);
}

uint8_t sourceSelectDataByte(uint8_t input) {
  if (input <= 12) {
	return static_cast<uint8_t>(0x0F + input);
  }
  return static_cast<uint8_t>(0x56 + input);
}

void sendJsonOkWithTx(const String& message, uint8_t zone, uint8_t command, uint8_t data) {
  String body = "{\"ok\":true,\"message\":\"" + message + "\"";
  body += ",\"txFrame\":\"" + commandFrameHex(zone, command, data) + "\"}";
  server.send(200, "application/json", body);
}

void sendJsonOkWithTxFollow(const String& message, uint8_t zone, uint8_t command, uint8_t data, uint8_t followCommand, uint8_t followData) {
  String body = "{\"ok\":true,\"message\":\"" + message + "\"";
  body += ",\"txFrame\":\"" + commandFrameHex(zone, command, data) + "\"";
  body += ",\"txFollowUp\":\"" + commandFrameHex(zone, followCommand, followData) + "\"}";
  server.send(200, "application/json", body);
}

void handleRoot() {
  server.send(200, "text/html", LYNC_CONTROL_PAGE);
}

void handleEthernetPage() {
  server.send(200, "text/html", LYNC_ETHERNET_PAGE);
}

void handleNamesPage() {
  server.send(200, "text/html", LYNC_NAMES_PAGE);
}

void handleRs232Page() {
  server.send(200, "text/html", LYNC_RS232_PAGE);
}

void handleApiEthernetSettings() {
  if (server.hasArg("dhcp")) {
	const String dhcpArg = server.arg("dhcp");
	ethernetConfig.dhcp = (dhcpArg == "1" || dhcpArg == "true");

	if (!ethernetConfig.dhcp) {
	  IPAddress ip;
	  IPAddress gateway;
	  IPAddress subnet;
	  if (!parseIpArg("ip", ip) || !parseIpArg("gateway", gateway) || !parseIpArg("subnet", subnet)) {
		sendJson(false, "ip, gateway, subnet required for static mode");
		return;
	  }
	  ethernetConfig.ip = ip;
	  ethernetConfig.gateway = gateway;
	  ethernetConfig.subnet = subnet;

	  IPAddress dns1;
	  if (parseIpArg("dns1", dns1)) {
		ethernetConfig.dns1 = dns1;
	  }
	  IPAddress dns2;
	  if (parseIpArg("dns2", dns2)) {
		ethernetConfig.dns2 = dns2;
	  }
	}

	saveEthernetConfig();
	server.send(200, "application/json", "{\"ok\":true,\"message\":\"saved; restarting\"}");
	delay(250);
	ESP.restart();
	return;
  }

  const String activeInterface = ethHasIp ? "ethernet" : (wifiHasIp ? "wifi" : "none");
  IPAddress currentIp = ethHasIp ? ETH.localIP() : WiFi.localIP();

  String body = "{\"ok\":true";
  body += ",\"active\":\"" + activeInterface + "\"";
  body += ",\"ethHasIp\":" + String(ethHasIp ? "true" : "false");
  body += ",\"wifiHasIp\":" + String(wifiHasIp ? "true" : "false");
  body += ",\"dhcp\":" + String(ethernetConfig.dhcp ? "true" : "false");
  body += ",\"ip\":\"" + ipToString(ethernetConfig.ip) + "\"";
  body += ",\"gateway\":\"" + ipToString(ethernetConfig.gateway) + "\"";
  body += ",\"subnet\":\"" + ipToString(ethernetConfig.subnet) + "\"";
  body += ",\"dns1\":\"" + ipToString(ethernetConfig.dns1) + "\"";
  body += ",\"dns2\":\"" + ipToString(ethernetConfig.dns2) + "\"";
  body += ",\"deviceIp\":\"" + ipToString(currentIp) + "\"";
  body += "}";
  server.send(200, "application/json", body);
}

void handleApiStatus() {
  String body = "{\"ok\":true,\"zones\":[";
  for (uint8_t z = 1; z <= LYNC_MAX_ZONES; ++z) {
	if (z > 1) body += ",";
	body += zoneJson(z);
  }
  body += "]}";
  server.send(200, "application/json", body);
}

void handleCmdPower() {
  uint8_t zone = 0;
  if (!parseZoneArg(zone) || !server.hasArg("on")) {
	sendJson(false, "zone and on required");
	return;
  }
  bool on = server.arg("on") == "1";
	const uint8_t data = on ? 0x57 : 0x58;
  if (!lync.powerZone(zone, on)) {
	sendJson(false, "command failed");
	return;
  }
	sendJsonOkWithTx("ok", zone, 0x04, data);
}

void handleCmdMute() {
  uint8_t zone = 0;
  if (!parseZoneArg(zone) || !server.hasArg("on")) {
	sendJson(false, "zone and on required");
	return;
  }
  bool on = server.arg("on") == "1";
	const uint8_t data = on ? 0x1E : 0x1F;
  if (!lync.muteZone(zone, on)) {
	sendJson(false, "command failed");
	return;
  }
	sendJsonOkWithTx("ok", zone, 0x04, data);
}

void handleCmdDnd() {
  uint8_t zone = 0;
  if (!parseZoneArg(zone) || !server.hasArg("on")) {
	sendJson(false, "zone and on required");
	return;
  }
  bool on = server.arg("on") == "1";
	const uint8_t data = on ? 0x59 : 0x5A;
  if (!lync.dndZone(zone, on)) {
	sendJson(false, "command failed");
	return;
  }
	sendJsonOkWithTx("ok", zone, 0x04, data);
}

void handleCmdInput() {
  uint8_t zone = 0;
  int input = 0;
  if (!parseZoneArg(zone) || !parseIntArg("input", input)) {
	sendJson(false, "zone and input required");
	return;
  }
	if (input < 1 || input > 18) {
	sendJson(false, "input must be 1..18");
	return;
  }
  const uint8_t inputByte = sourceSelectDataByte(static_cast<uint8_t>(input));
  if (!lync.selectInput(zone, static_cast<uint8_t>(input))) {
	sendJson(false, "command failed");
	return;
  }

  zoneState[zone].known = true;
  zoneState[zone].input = static_cast<uint8_t>(input);
	zoneHold[zone].inputUntilMs = millis() + kStateHoldMs;
  broadcastZone(zone);

	sendJsonOkWithTx("ok", zone, 0x04, inputByte);
}

void handleCmdVolume() {
  uint8_t zone = 0;
	int value = 0;
  uint8_t level = 0;

  if (!parseZoneArg(zone)) {
	sendJson(false, "zone required");
	return;
  }

  if (server.hasArg("level")) {
	value = server.arg("level").toInt();
	if (value < 0 || value > 59) {
	  sendJson(false, "level must be 0..59");
	  return;
	}
	level = static_cast<uint8_t>(value);
  } else if (server.hasArg("db")) {
	value = server.arg("db").toInt();
	if (value > 0 || value < -59) {
	  sendJson(false, "db must be -59..0");
	  return;
	}
	level = static_cast<uint8_t>(-value);
  } else {
	sendJson(false, "level required");
	return;
  }

	const uint8_t dataByte = static_cast<uint8_t>(196 + level);
  if (dataByte < 0xC4 || dataByte > 0xFF) {
	sendJson(false, "internal volume byte range error");
	return;
  }

  if (!lync.setVolumeDb(zone, level)) {
	sendJson(false, "command failed");
	return;
  }

  zoneState[zone].known = true;
  zoneState[zone].volumeDb = static_cast<int8_t>(static_cast<int>(level) - 60);
	zoneHold[zone].volumeUntilMs = millis() + kStateHoldMs;
  broadcastZone(zone);

  char hexByte[5] = {0};
  snprintf(hexByte, sizeof(hexByte), "%02X", dataByte);
	String body = "{\"ok\":true,\"message\":\"ok\",\"level\":" + String(level);
  body += ",\"dataByte\":" + String(dataByte);
  body += ",\"dataHex\":\"0x" + String(hexByte) + "\"";
  body += ",\"txFrame\":\"" + commandFrameHex(zone, 0x15, dataByte) + "\"";
  body += ",\"txFollowUp\":\"" + commandFrameHex(zone, 0x04, 0x1F) + "\"}";
  server.send(200, "application/json", body);
}

void handleCmdTreble() {
  uint8_t zone = 0;
  int value = 0;
  if (!parseZoneArg(zone) || !parseIntArg("db", value)) {
	sendJson(false, "zone and db required");
	return;
  }
	if (value < -10 || value > 10) {
	sendJson(false, "db must be -10..10");
	return;
  }
	const uint8_t data = static_cast<uint8_t>(static_cast<int8_t>(value));
  if (!lync.setTrebleDb(zone, static_cast<int8_t>(value))) {
	sendJson(false, "command failed");
	return;
  }

  const uint8_t currentInput = zoneState[zone].input;
  if (currentInput < 1 || currentInput > 18) {
	sendJson(false, "treble set, but current source is unknown for follow-up");
	return;
  }
  if (!lync.selectInput(zone, currentInput)) {
	sendJson(false, "treble set, but source follow-up failed");
	return;
  }

  zoneState[zone].known = true;
  zoneState[zone].trebleDb = static_cast<int8_t>(value);
	zoneHold[zone].trebleUntilMs = millis() + kStateHoldMs;
  broadcastZone(zone);

	sendJsonOkWithTxFollow("ok", zone, 0x17, data, 0x04, sourceSelectDataByte(currentInput));
}

void handleCmdBass() {
  uint8_t zone = 0;
  int value = 0;
  if (!parseZoneArg(zone) || !parseIntArg("db", value)) {
	sendJson(false, "zone and db required");
	return;
  }
	if (value < -10 || value > 10) {
	sendJson(false, "db must be -10..10");
	return;
  }
	const uint8_t data = static_cast<uint8_t>(static_cast<int8_t>(value));
  if (!lync.setBassDb(zone, static_cast<int8_t>(value))) {
	sendJson(false, "command failed");
	return;
  }

  const uint8_t currentInput = zoneState[zone].input;
  if (currentInput < 1 || currentInput > 18) {
	sendJson(false, "bass set, but current source is unknown for follow-up");
	return;
  }
  if (!lync.selectInput(zone, currentInput)) {
	sendJson(false, "bass set, but source follow-up failed");
	return;
  }

  zoneState[zone].known = true;
  zoneState[zone].bassDb = static_cast<int8_t>(value);
	zoneHold[zone].bassUntilMs = millis() + kStateHoldMs;
  broadcastZone(zone);

	sendJsonOkWithTxFollow("ok", zone, 0x18, data, 0x04, sourceSelectDataByte(currentInput));
}

void handleCmdBalance() {
  uint8_t zone = 0;
  int value = 0;
  if (!parseZoneArg(zone) || !parseIntArg("db", value)) {
	sendJson(false, "zone and db required");
	return;
  }
  if (!lync.setBalanceDb(zone, static_cast<int8_t>(value))) {
	sendJson(false, "command failed");
	return;
  }
  sendJson(true, "ok");
}

void handleCmdQuery() {
  if (!lync.queryAllZoneStatus()) {
	sendJson(false, "query failed");
	return;
  }
	sendJsonOkWithTx("ok", 0x00, 0x05, 0x00);
}

void handleApiRs232State() {
  if (server.hasArg("paused")) {
	const bool newPaused = (server.arg("paused") == "1" || server.arg("paused") == "true");
	if (newPaused != rs232Paused) {
	  rs232Paused = newPaused;
	  if (rs232Paused) {
		while (LyncUart.available() > 0) {
		  LyncUart.read();
		}
		appendRs232Log("[MODE] RS232 paused (auto communication stopped)");
	  } else {
		appendRs232Log("[MODE] RS232 resumed");
	  }
	}
  }

  String body = "{\"ok\":true,\"paused\":";
  body += rs232Paused ? "true" : "false";
  body += "}";
  server.send(200, "application/json", body);
}

void handleApiRs232Send() {
  if (!server.hasArg("bytes")) {
	sendJson(false, "bytes required");
	return;
  }

  uint8_t raw[128] = {0};
  size_t rawLen = 0;
  String error;
  if (!parseHexCommandString(server.arg("bytes"), raw, rawLen, sizeof(raw), error)) {
	sendJson(false, error);
	return;
  }

  const size_t written = LyncUart.write(raw, rawLen);
  LyncUart.flush();

  const String hex = rs232BytesToHex(raw, rawLen);
  appendRs232Log("[TX] " + hex);

  String body = "{\"ok\":";
  body += (written == rawLen) ? "true" : "false";
  body += ",\"written\":" + String(static_cast<unsigned>(written));
  body += ",\"length\":" + String(static_cast<unsigned>(rawLen));
  body += ",\"tx\":\"" + jsonEscape(hex) + "\"}";
  server.send((written == rawLen) ? 200 : 400, "application/json", body);
}

void handleApiRs232Log() {
  String body = "{\"ok\":true,\"paused\":";
  body += rs232Paused ? "true" : "false";
  body += ",\"log\":\"" + jsonEscape(rs232Log) + "\"}";
  server.send(200, "application/json", body);
}

void handleApiRs232LogClear() {
  rs232Log = "";
  sendJson(true, "cleared");
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
	server.on("/names", HTTP_GET, handleNamesPage);
	server.on("/ethernet", HTTP_GET, handleEthernetPage);
	server.on("/rs232", HTTP_GET, handleRs232Page);
  server.on("/api/status", HTTP_GET, handleApiStatus);
	server.on("/api/names", HTTP_GET, handleApiNames);
	server.on("/api/network/ethernet", HTTP_ANY, handleApiEthernetSettings);
	server.on("/api/network/wifi/clear", HTTP_ANY, handleApiWifiClear);
	server.on("/api/name/zone", HTTP_GET, handleApiZoneNameByIndex);
  server.on("/api/name/source", HTTP_GET, handleApiSourceNameByIndex);
  server.on("/api/names/zone", HTTP_ANY, handleApiSetZoneName);
  server.on("/api/names/source", HTTP_ANY, handleApiSetSourceName);
	server.on("/api/names/zone/sources", HTTP_ANY, handleApiSetZoneSourceMask);
  server.on("/api/cmd/power", HTTP_ANY, handleCmdPower);
  server.on("/api/cmd/mute", HTTP_ANY, handleCmdMute);
  server.on("/api/cmd/dnd", HTTP_ANY, handleCmdDnd);
  server.on("/api/cmd/input", HTTP_ANY, handleCmdInput);
  server.on("/api/cmd/volume", HTTP_ANY, handleCmdVolume);
  server.on("/api/cmd/treble", HTTP_ANY, handleCmdTreble);
  server.on("/api/cmd/bass", HTTP_ANY, handleCmdBass);
  server.on("/api/cmd/balance", HTTP_ANY, handleCmdBalance);
  server.on("/api/cmd/query", HTTP_ANY, handleCmdQuery);
	server.on("/api/rs232/state", HTTP_ANY, handleApiRs232State);
  server.on("/api/rs232/send", HTTP_ANY, handleApiRs232Send);
  server.on("/api/rs232/log", HTTP_GET, handleApiRs232Log);
  server.on("/api/rs232/log/clear", HTTP_ANY, handleApiRs232LogClear);
  server.onNotFound([]() { server.send(404, "application/json", "{\"ok\":false,\"message\":\"not found\"}"); });
}

#if LYNC_HAS_WEBSOCKET
void onWsEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED) {
	IPAddress ip = ws.remoteIP(clientNum);
	Serial.printf("WS client %u connected from %u.%u.%u.%u\n", clientNum, ip[0], ip[1], ip[2], ip[3]);
	for (uint8_t z = 1; z <= LYNC_MAX_ZONES; ++z) {
	  if (zoneState[z].known) {
		String payloadMsg = "{\"type\":\"zone\",\"data\":" + zoneJson(z) + "}";
		ws.sendTXT(clientNum, payloadMsg);
	  }
	}
  }
}
#endif

void processLyncResponses() {
  LyncSerial::ParsedMessage msg;
  while (lync.poll(msg)) {
	if (msg.type == LyncSerial::RxType::ZoneStatus) {
	  uint8_t z = msg.zone;
	  if (z >= 1 && z <= LYNC_MAX_ZONES) {
		zoneState[z].known = true;
		zoneState[z].powerOn = msg.zoneStatus.powerOn;
		zoneState[z].muteOn = msg.zoneStatus.muteOn;
		zoneState[z].dndOn = msg.zoneStatus.dndOn;
		if (!holdActive(zoneHold[z].inputUntilMs)) {
		  zoneState[z].input = msg.zoneStatus.input;
		}
		if (!holdActive(zoneHold[z].volumeUntilMs)) {
		  zoneState[z].volumeDb = msg.zoneStatus.volumeDb;
		}
		if (!holdActive(zoneHold[z].trebleUntilMs)) {
		  zoneState[z].trebleDb = msg.zoneStatus.trebleDb;
		}
		if (!holdActive(zoneHold[z].bassUntilMs)) {
		  zoneState[z].bassDb = msg.zoneStatus.bassDb;
		}
		zoneState[z].balanceDb = msg.zoneStatus.balanceDb;
		broadcastZone(z);
	  }
	} else if (msg.type == LyncSerial::RxType::ErrorStatus) {
	  broadcastInfo("Lync error code: " + String(msg.errorCode));
	} else if (msg.type == LyncSerial::RxType::Mp3FileName || msg.type == LyncSerial::RxType::Mp3ArtistName) {
	  broadcastInfo(String(msg.text));
	}
  }
}

void onNetworkEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_ETH_GOT_IP) {
	ethHasIp = true;
	Serial.print("Ethernet IP: ");
	Serial.println(ETH.localIP());
	return;
  }
  if (event == ARDUINO_EVENT_ETH_DISCONNECTED || event == ARDUINO_EVENT_ETH_STOP) {
	ethHasIp = false;
	return;
  }
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
	wifiHasIp = true;
	Serial.print("WiFi IP: ");
	Serial.println(WiFi.localIP());
	return;
  }
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
	wifiHasIp = false;
	return;
  }
}

bool connectEthernet(uint32_t timeoutMs) {
  Serial.println("Attempting Ethernet connection...");

  if (!ethStarted) {
	SPI.begin(ETH_SPI_SCK_PIN, ETH_SPI_MISO_PIN, ETH_SPI_MOSI_PIN, ETH_SPI_CS_PIN);
	ethStarted = ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR, ETH_SPI_CS_PIN, ETH_SPI_IRQ_PIN, ETH_SPI_RST_PIN, SPI);
	if (!ethStarted) {
	  Serial.println("Ethernet init failed");
	  return false;
	}
  }

  if (!ethernetConfig.dhcp) {
	if (!ETH.config(ethernetConfig.ip, ethernetConfig.gateway, ethernetConfig.subnet, ethernetConfig.dns1, ethernetConfig.dns2)) {
	  Serial.println("Ethernet static config failed");
	}
  }

  const uint32_t startMs = millis();
  while (!ethHasIp && (millis() - startMs) < timeoutMs) {
	delay(100);
  }

  if (!ethHasIp) {
	Serial.println("Ethernet did not acquire IP");
	return false;
  }

  Serial.print("Ethernet connected. IP: ");
  Serial.println(ETH.localIP());
  return true;
}

bool connectWifi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
	WiFi.begin();
  Serial.print("Connecting to WiFi");

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeoutMs) {
	delay(500);
	Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
	Serial.println("WiFi connection failed");
	return false;
  }

  wifiHasIp = true;
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool hasSavedWifiCredentials() {
	WiFi.mode(WIFI_STA);
  const String ssid = WiFi.SSID();
  return ssid.length() > 0;
}

bool startWifiProvisioningPortal(uint32_t timeoutSec) {
  WiFiManager wm;
  wm.setConfigPortalTimeout(timeoutSec);
  Serial.println("Starting WiFiManager captive portal...");

  if (!wm.autoConnect(WIFI_PORTAL_SSID)) {
	Serial.println("WiFiManager portal timed out or failed");
	return false;
  }

  wifiHasIp = (WiFi.status() == WL_CONNECTED);
  if (wifiHasIp) {
	Serial.print("WiFi provisioned. IP: ");
	Serial.println(WiFi.localIP());
  }
  return wifiHasIp;
}

void connectNetwork() {
  if (connectEthernet(12000)) {
	return;
  }

  if (hasSavedWifiCredentials()) {
	if (connectWifi(15000)) {
	  return;
	}
  }

  startWifiProvisioningPortal(180);
}

void setupOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  ArduinoOTA.onStart([]() {
	const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
	Serial.printf("OTA start (%s)\n", type);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
	const unsigned int percent = (total == 0U) ? 0U : (progress * 100U) / total;
	Serial.printf("OTA progress: %u%%\r", percent);
  });

  ArduinoOTA.onEnd([]() {
	Serial.println("\nOTA complete");
  });

  ArduinoOTA.onError([](ota_error_t error) {
	Serial.printf("OTA error [%u]\n", static_cast<unsigned int>(error));
  });

  ArduinoOTA.begin();
  Serial.printf("OTA ready. Hostname: %s.local\n", OTA_HOSTNAME);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  WiFi.onEvent(onNetworkEvent);

  LyncUart.begin(LYNC_BAUD, SERIAL_8N1, LYNC_UART_RX_PIN, LYNC_UART_TX_PIN);
  lync.setEchoMode(true);

  loadNamesFromPrefs();

	connectNetwork();
	setupOta();
  setupRoutes();
  server.begin();
  Serial.println("HTTP server started on port 80");

#if LYNC_HAS_WEBSOCKET
  ws.begin();
  ws.onEvent(onWsEvent);
  Serial.println("WebSocket server started on port 81");
#else
  Serial.println("WebSockets disabled: install arduinoWebSockets to enable live push");
#endif

  lync.queryAllZoneStatus();
}

void loop() {
	ArduinoOTA.handle();
  server.handleClient();
#if LYNC_HAS_WEBSOCKET
  ws.loop();
#endif

  if (rs232Paused) {
	capturePausedRs232Rx();
	return;
  }

  processLyncResponses();

  static uint32_t lastSync = 0;
  const uint32_t now = millis();
	if (now - lastSync > 30000) {
	lastSync = now;
	lync.queryAllZoneStatus();
  }
}
