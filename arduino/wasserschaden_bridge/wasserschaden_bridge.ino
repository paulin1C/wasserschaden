#include <Arduino.h>
#include <painlessMesh.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "settings.cpp" //contains passwords

#define POWER_LED D0
#define WLAN_LED D2
#define MESH_LED D5
#define MQTT_LED D6
#define MQTT_RX_LED D1
#define MESH_RX_LED D3

#define   MESH_PORT       5555
#define HOSTNAME "MQTT_Bridge"

// Prototypes
void receivedCallback( const uint32_t &from, const String &msg );
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttConnect();
void mqtt_led_off();
void mesh_led_off();

char* message_buff;

const String from_gateway = String(MQTT_TOPIC)+"/from/gateway";

IPAddress getlocalIP();

IPAddress no_ip(0, 0, 0, 0);
IPAddress myIP(0, 0, 0, 0);
IPAddress mqttBroker(10, 8, 0, 1);

painlessMesh  mesh;
WiFiClient wifiClient;
PubSubClient mqttClient(mqttBroker, 1883, mqttCallback, wifiClient);


Scheduler userScheduler;
Task reconnect(1000, TASK_FOREVER, &mqttConnect);
Task mqtt_blink(50, 1, &mqtt_led_off);
Task mesh_blink(50, 1, &mesh_led_off);

char* convert_for_mqtt(int input) {
  String pubString = String(input);
  pubString.toCharArray(message_buff, pubString.length() + 1);
  return message_buff;
}

void setup() {
  pinMode(POWER_LED, OUTPUT);
  pinMode(WLAN_LED, OUTPUT);
  pinMode(MESH_LED, OUTPUT);
  pinMode(MQTT_LED, OUTPUT);
  pinMode(MQTT_RX_LED, OUTPUT);
  pinMode(MESH_RX_LED, OUTPUT);
  Serial.begin(115200);
  digitalWrite(POWER_LED, HIGH);
  mesh.setDebugMsgTypes( ERROR | STARTUP | CONNECTION );  // set before init() so that you can see startup messages

  // Channel set to 6. Make sure to use the same channel for your mesh and for you other
  // network (STATION_SSID)
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, 1);
  mesh.onReceive(&receivedCallback);

  mesh.onNewConnection([](const uint32_t nodeId) {
    digitalWrite(MESH_LED, HIGH);
    if (nodeId != 0) {
      String topic = String(MQTT_TOPIC)+"/from/" + String(nodeId);
      mqttClient.publish((topic.c_str()), "connected");
      Serial.printf("New Connection \n", nodeId);
    }
  });

  mesh.onDroppedConnection([](const uint32_t nodeId) {
    if (nodeId != 0) {
      String topic = String(MQTT_TOPIC)+"/from/" + String(nodeId);
      mqttClient.publish(topic.c_str(), "dropped");
      Serial.printf("Dropped Connection %u\n", nodeId);
    }
    if (mesh.subConnectionJson().length() < 3) {
      digitalWrite(MESH_LED, LOW);
    }
  });

  mesh.stationManual(STATION_SSID, STATION_PASSWORD);
  mesh.setHostname(HOSTNAME);

  userScheduler.addTask(mesh_blink);
  userScheduler.addTask(mqtt_blink);
  userScheduler.addTask(reconnect);
}

void mqttConnect() {
  if (mqttClient.connect(String("pmC_"+String(MQTT_TOPIC)).c_str()),from_gateway,1,false,"dead") {
    digitalWrite(MQTT_LED, HIGH);
    Serial.println("connected to mqtt");
    mqttClient.publish(from_gateway.c_str(), "Ready!");
    mqttClient.subscribe(String(String(MQTT_TOPIC)+"/to/#").c_str(), 0);
    reconnect.disable();
  } else {
    digitalWrite(MQTT_LED, LOW);
  }
}

void mqtt_led_off() {
  if (mqtt_blink.isLastIteration()) {
    digitalWrite(MQTT_RX_LED, LOW);
    mqtt_blink.disable();
    mqtt_blink.setIterations(2);
  }
}

void mesh_led_off() {
  if (mesh_blink.isLastIteration()) {
    digitalWrite(MESH_RX_LED, LOW);
    mesh_blink.disable();
    mesh_blink.setIterations(2);
  }
}

void loop() {
  mesh.update();
  mqttClient.loop();

  if (myIP != getlocalIP()) {
    analogWrite(WLAN_LED, 200);
    myIP = getlocalIP();
    Serial.println("My IP is " + myIP.toString());
    mqttConnect();
  }
  if (myIP == no_ip) {
    digitalWrite(WLAN_LED, LOW);
  } else {
    analogWrite(WLAN_LED, 200);
  }
  if (!mqttClient.connected() and !reconnect.isEnabled()) {
    reconnect.enable();
    Serial.println("enabled reconnect task");
  }
  userScheduler.execute();
}

void receivedCallback( const uint32_t &from, const String &msg ) {
  digitalWrite(MESH_RX_LED, HIGH);
  Serial.printf("bridge: Received from %u msg=%s\n", from, msg.c_str());
  String topic = String(MQTT_TOPIC)+"/from/" + String(from);
  mqttClient.publish(topic.c_str(), msg.c_str());
  mesh_blink.enable();
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  digitalWrite(MQTT_RX_LED, HIGH);
  char* cleanPayload = (char*)malloc(length + 1);
  payload[length] = '\0';
  memcpy(cleanPayload, payload, length + 1);
  String msg = String(cleanPayload);
  free(cleanPayload);

  String targetStr = String(topic).substring(16);

  if (targetStr == "gateway")
  {
    if (msg == "getNodes")
    {
      //Serial.println( mesh.subConnectionJson().c_str());
      mqttClient.publish(from_gateway.c_str(), mesh.subConnectionJson().c_str());
    }
  }
  else if (targetStr == "broadcast")
  {
    mesh.sendBroadcast(msg);
  }
  else
  {
    uint32_t target = strtoul(targetStr.c_str(), NULL, 10);
    if (mesh.isConnected(target))
    {
      mesh.sendSingle(target, msg);
    }
    else
    {
      mqttClient.publish(from_gateway.c_str(), "Client not connected!");
    }
  }
  mqtt_blink.enable();
}

IPAddress getlocalIP() {
  return IPAddress(mesh.getStationIP());
}