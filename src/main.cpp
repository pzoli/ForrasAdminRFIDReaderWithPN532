#include <Arduino.h>
#include <SPI.h>
#include "EEpromWriteAnything.h"
#include <Ethernet.h>
#include <PN532_I2C.h>
#include <PN532.h>

#include <ArduinoJson.h>
JsonDocument doc;

#define BUZZER_PIN 7

#define RGB_R 2
#define RGB_G 3
#define RGB_B 4

// #define DEBUG
#define DHCP
#define RGB_RESPONSE_ENABLED
#define BUZZER_ENABLED

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

struct NetConfig
{
  char state[10];
  uint8_t mac[6];
  uint8_t usedhcp;
  uint8_t serverip[4];
  uint8_t ip[4];
  uint8_t subnet[4];
  uint8_t gateway[4];
  uint8_t dnsserver[4];
  uint16_t serverport;
  char request[80];
} conf;

uint8_t selectAid[] = {
    0x00,                                     /* CLA */
    0xA4,                                     /* INS: Select File */
    0x04,                                     /* P1: Select by AID */
    0x00,                                     /* P2: First or only occurrence */
    0x07,                                     /* Lc: AID hossza (7 bájt) */
    0xA0, 0x00, 0x00, 0x02, 0x47, 0x10, 0x01, /* Az AID-ed */
    0x00                                      /* Le: Várható válaszhossz nincs meghatározva */
};

#ifdef DEBUG
void printIPToSerial(String ipname, IPAddress addr)
{
  char ipno[16];
  sprintf(ipno, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
  Serial.print(ipname);
  Serial.println(ipno);
};
#endif
void initEthernet()
{
#ifdef DHCP
  if (conf.usedhcp == 1)
  {
    Serial.println(F("Ethernet configure useing DHCP"));
    Ethernet.begin(conf.mac);
    Ethernet.maintain();
  }
  else
  {
#endif
    Serial.println(F("Ethernet configure using fix IP"));
    Ethernet.begin(conf.mac, conf.ip, conf.dnsserver, conf.gateway, conf.subnet);
#ifdef DHCP
  }
#endif
}

String getMACasString(uint8_t *mac)
{
  char macno[32];
  sprintf(macno, "%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macno);
}
#ifdef RGB_RESPONSE_ENABLED
void setColor(int redValue, int greenValue, int blueValue)
{
  analogWrite(RGB_R, redValue);
  analogWrite(RGB_G, greenValue);
  analogWrite(RGB_B, blueValue);
}
#endif

String webResult = "";

void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
#ifdef RGB_RESPONSE_ENABLED
  setColor(255, 0, 0);
  delay(1000);
#endif
#ifdef DEBUG
  while (!Serial)
  {
    ;
  }
#endif
  EEPROM_readAnything(1, conf);
  bool configured = String(conf.state).startsWith("CONFIG");
  if (configured)
  {
    initEthernet();
  }
  Serial.println(configured ? F("Configured") : F("Not confugured yet"));

#ifdef DEBUG
  if (configured)
  {
    Serial.print(F("reader ip: "));
    Serial.println(Ethernet.localIP());
    printIPToSerial(F("server ip: "), conf.serverip);
  }
#endif

#ifdef RGB_RESPONSE_ENABLED
  setColor(0, 255, 0);
  delay(1000);
#endif
  Serial.println(F("init RFID reader..."));
  nfc.begin();
  nfc.SAMConfig();
  Serial.println(F("RFID reader initialized."));
#ifdef DEBUG
  // Verzió lekérdezése
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata)
  {
    Serial.println(F("PN532 module not found. Check connections."));
    while (1)
      ; // Állj meg itt
  }
  Serial.print(F("PN532 Firmware version: "));
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.print((versiondata >> 16) & 0xFF, HEX);
  Serial.print(".");
  Serial.println((versiondata >> 8) & 0xFF, HEX);
#endif
  Serial.println(F("Waiting for phone or card..."));
  webResult.reserve(128);
#ifdef RGB_RESPONSE_ENABLED
  setColor(0, 0, 255);
  delay(1000);
  setColor(255, 255, 255);  
#endif
#ifdef BUZZER_ENABLED
  tone(BUZZER_PIN, 1000);
  delay(250);
  noTone(BUZZER_PIN);
#endif
}

void processServerResponse() {
  if (webResult.equals(F("{\"RESPONSE\":\"OK\"}"))) {
    Serial.println(F("Success response beep"));
    #ifdef RGB_RESPONSE_ENABLED
      setColor(0, 255, 0);
      tone(BUZZER_PIN, 1000, 250);
      delay(250);
      setColor(255, 255, 255);
    #endif
  } else {
    Serial.println(F("Failure or Unknown case response beep"));
    #ifdef RGB_RESPONSE_ENABLED
      setColor(255, 0, 0);
      tone(BUZZER_PIN, 1000, 250);
      delay(500);
      tone(BUZZER_PIN, 1000, 250);
      setColor(255, 255, 255);
    #endif
  }
}

EthernetClient webClient;
bool dataSent = false;
bool inJSON = false;
String command = "";
int timer = 0;

void loop()
{
  if (conf.usedhcp == 1)
  {
    Ethernet.maintain();
  }
  int available = webClient.available();
  while(available > 0)
  {
    char buff[64];
    int cnt = webClient.readBytes(buff, available > sizeof(buff) ? sizeof(buff) : available);
    for (int i = 0; i < cnt; i++)
    {
#ifdef DEBUG
      Serial.print(buff[i]);
#endif
      if (buff[i] == '{' && !inJSON)
      {
        inJSON = true;
        webResult = "";
      }
      else if (buff[i] == '}' && inJSON)
      {
        inJSON = false;
        webResult += buff[i];
        processServerResponse();
      }
      if (inJSON)
      {
        webResult += buff[i];
      }
    }
#ifdef DEBUG
    Serial.write(buff, cnt);
#endif
    available = webClient.available();
  }

  if (!webClient.connected() && dataSent)
  {
    webClient.stop();
    dataSent = false;
  }
  uint8_t success;
  uint8_t response[32];
  uint8_t responseLength = 32;
  success = nfc.inListPassiveTarget();
  // Look for new cards
  if (success && timer == 0)
  {
    timer = 30; // about 3 seconds delay
    String cardId;
    String cardType = "RF1";
    digitalWrite(LED_BUILTIN, HIGH);
    if (nfc.inDataExchange(selectAid, sizeof(selectAid), response, &responseLength))
    {
      // Success: this is a phone, response for AID
      if (responseLength > 2)
      {
        for (uint8_t i = 0; i < responseLength - 2; i++)
        {
          cardId += String(response[i], HEX);
        }
      }
      cardType = "VRF1";
    }
    else
    {
      // Not a phone, read UID
      uint8_t uid[7];
      uint8_t uidLength;

      if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
      {
#ifdef DEBUG
        Serial.print("Card UID: ");
#endif
        for (uint8_t i = 0; i < uidLength; i++)
        {
          char chrHex[2];
          sprintf(chrHex, "%02X", uid[i]);
          cardId += chrHex;
        }
      }
    }
#ifdef DEBUG
    Serial.println();
    Serial.print(F("cardid:"));
    Serial.println(cardId);
#endif
    if (webClient.connect(conf.serverip, conf.serverport)) // 8080
    {
      webClient.print(F("GET "));
      String request = String(conf.request);
      request.replace(F("%RID%"), getMACasString(conf.mac));
      request.replace(F("%CID%"), cardId);
      request.replace(F("%TYPE%"), cardType);
#ifdef DEBUG
      Serial.println();
      Serial.print(F("request:"));
      Serial.println(request);
#endif
      webClient.print(request);
      webClient.println(F(" HTTP/1.1"));
      webClient.print(F("Host: "));
      String serverIp = doc["serverip"];
      webClient.print(serverIp);
      webClient.println();
      webClient.println(F("Connection: close"));
      webClient.println();
      dataSent = true;
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("connection failed"));
#endif
#ifdef RGB_RESPONSE_ENABLED &&BUZZER_ENABLED
      setColor(0, 0, 255);
      tone(BUZZER_PIN, 500);
      delay(2000);
      noTone(BUZZER_PIN);
      setColor(255, 255, 255);
#endif
    }
  }
  if (timer > 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    timer -= 1;
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }    
}
