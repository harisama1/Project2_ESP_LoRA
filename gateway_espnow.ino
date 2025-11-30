#include <WiFi.h>
#include <esp_now.h>
#include <PubSubClient.h>

// ===================== WIFI + MQTT =====================
const char* ssid = "Wifi_for_esp32";
const char* password = "123456789";

const char* mqtt_server = "demo.thingsboard.io";
const int   mqtt_port   = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// ===================== ESP-NOW MESSAGE =====================
typedef struct {
  int id;
  float temperature;
  float current;
  float voltage;
} struct_message;

typedef struct {
  bool requestData;
} request_message;

// ===================== NODE STRUCT =====================
typedef struct {
  uint8_t mac[6];
  int id;

  float temperature;
  float current;
  float voltage;

  bool received;
  const char* token;
} Node;

// ===================== NODE LIST =====================
Node nodes[] = {
  { {0xA0,0xA3,0xB3,0x29,0xBE,0x5C}, 1, 0,0,0, false, "FDxdurpOltHZcwzozd6e" },
  { {0x88,0x57,0x21,0x95,0x3D,0xA8}, 2, 0,0,0, false, "RQuEFXGAv0Y9qzBVKd48" }
};

const int NODE_COUNT = sizeof(nodes)/sizeof(nodes[0]);

// ===================== FSM =====================
int currentNode = 0;
unsigned long lastCall = 0;
unsigned long waitStart = 0;

// ===================== MQTT ======================
bool connectMQTT(const char* token)
{
  if (client.connected()) client.disconnect();

  Serial.print("MQTT connect (token as clientID): ");
  Serial.println(token);

  if (client.connect(token, token, "")) {
    Serial.println("MQTT OK");
    return true;
  }

  Serial.print("MQTT ERROR rc=");
  Serial.println(client.state());
  return false;
}

void sendTelemetry(Node& n)
{
  if (!connectMQTT(n.token)) return;

  String payload = "{";
  payload += "\"temperature\":" + String(n.temperature) + ",";
  payload += "\"current\":"     + String(n.current) + ",";
  payload += "\"voltage\":"     + String(n.voltage);
  payload += "}";

  client.publish("v1/devices/me/telemetry", payload.c_str());

  Serial.print("Sent TB (Node ");
  Serial.print(n.id);
  Serial.print("): ");
  Serial.println(payload);
}

// ===================== ESP-NOW CALLBACK =====================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incoming, int len)
{
  struct_message d;
  memcpy(&d, incoming, sizeof(d));

  Serial.println("\n--- DATA RECEIVED ---");
  Serial.printf("Node %d\n", d.id);
  Serial.printf("Temp: %.2f\n", d.temperature);
  Serial.printf("Current: %.2f\n", d.current);
  Serial.printf("Voltage: %.2f\n", d.voltage);

  // Lưu vào struct tương ứng
  for (int i = 0; i < NODE_COUNT; i++) {
    if (nodes[i].id == d.id) {
      nodes[i].temperature = d.temperature;
      nodes[i].current     = d.current;
      nodes[i].voltage     = d.voltage;
      nodes[i].received    = true;
      break;
    }
  }
}

// ===================== SEND REQUEST =====================
void sendRequest(Node& n)
{
  request_message req = { true };

  esp_err_t r = esp_now_send(n.mac, (uint8_t*)&req, sizeof(req));
  Serial.printf("Request → Node %d : %s\n", n.id, esp_err_to_name(r));
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  client.setServer(mqtt_server, mqtt_port);

  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv);

  // add peers
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  peer.channel = 0;
  peer.encrypt = false;

  for (int i = 0; i < NODE_COUNT; i++) {
    memcpy(peer.peer_addr, nodes[i].mac, 6);
    esp_now_add_peer(&peer);
  }

  Serial.println("Gateway STARTED\n");
}

// ===================== LOOP =====================
void loop() {
  unsigned long now = millis();

  // Mỗi 5 phút
  if (now - lastCall >= 300000 || lastCall == 0) {
    lastCall = now;
    currentNode = 0;

    for (int i=0; i<NODE_COUNT; i++) nodes[i].received = false;

    Serial.println("=== START REQUEST CYCLE ===");
    sendRequest(nodes[currentNode]);
    waitStart = now;
  }

  // Chờ node trả lời hoặc timeout
  if (nodes[currentNode].received || now - waitStart > 2000) {

    currentNode++;

    if (currentNode < NODE_COUNT) {
      sendRequest(nodes[currentNode]);
      waitStart = now;
    }
    else {
      Serial.println("\n=== ALL DATA RECEIVED → SENDING TB ===");

      for (int i=0; i<NODE_COUNT; i++)
        sendTelemetry(nodes[i]);

      Serial.println("\n=== CYCLE DONE ===\n");
    }
  }
}
