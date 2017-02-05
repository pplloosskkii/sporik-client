/*
 
*/

#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>


// VARIANT 0
byte mac[] = { 0xb6,0xaf,0x98,0xda,0x47,0x30 };
IPAddress ip(172, 16, 0, 129);
const char* myAddress = "sporik0";

// VARIANT 1
//byte mac[] = {  0x8a, 0xab, 0xe4, 0x34, 0x25, 0x56 };
//IPAddress ip(172, 16, 0, 130); 
//const char* myAddress = "sporik1";

IPAddress myDns(172, 16, 0, 4);
IPAddress gateway(172, 16, 0, 4);
IPAddress subnet(255, 255, 255, 0);
IPAddress server(192, 168, 1, 99);
const int port = 1883;



bool isRegistered = false;

long regulationValue = 0;
long elapsedTime = 0;

bool regulationInProgress = false;
int measurementValue = 0;

const byte outputTogglePin = 2;
const byte inputZeroCrossPin = 3;
const byte inputAnalogPin = A2;

EthernetClient ethClient;
PubSubClient client(ethClient);


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String pl = str(payload, length);
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(pl);
 

  if (!strcmp(topic,"sporik/register")) {
    onRegistered(json);
  } else if (!strcmp(topic,"sporik/regulate")) {
    onRegulation(json);
  } else {
    Serial.println(F("unknown topic:"));
    Serial.println(topic);
  }
}

String str(byte * what, unsigned int len) {
  String ret = "";
  for (int i=0;i<len;i++) {
    ret = ret + (char)what[i];
  }
  return ret;
}

/* on registration:
 * - if myAddress is same as in reply, device was registered ok. 
 * - Start sending data and start receiving for on/off
 */
void onRegistered(JsonObject& json) {
  Serial.println("onRegistered()");
  if (!strcmp(json["address"], myAddress)) {
    isRegistered = true;
    client.subscribe("sporik/regulate");
  } else {
    Serial.println(F("wrong reply on register:"));
    json.printTo(Serial);
  }
}


/* on regulation:
 * - if myAddress is same as in reply and device is registered
 * - set level of regulation
 */
void onRegulation(JsonObject& json) {
  Serial.println("onRegulation()");
  if (!strcmp(json["address"], myAddress) && isRegistered && (int)json["value"] >= 0 && (int)json["value"] <= 100) {
    regulationValue = parsePercentToTime((int)json["value"]);
    Serial.print(F("Regulating to "));
    Serial.println(regulationValue);
    char pub[40];
    sprintf(pub, "{ \"address\":\"%s\", \"value\":\"%i\"}", myAddress, parseTimeToPercent(regulationValue));
    client.publish("sporik/triac-value", pub);
  } else {
    Serial.println(F("wrong reply on regulate:"));
    json.printTo(Serial);
  }
}

long parsePercentToTime(int value) {
  long sum = 0;
  int maxSin = 11458;
  for (float i = 0; i <= 180; i++) {
    long x = round(sin(i * PI / 180) * 100);
    sum = sum + x;
    long m = round((100 * sum) / maxSin);
    if (m >= value) {
      return round((float)(i / 180) * 10000);
    }
  }
}

int parseTimeToPercent(long value) {
  long sum = 0;
  int maxSin = 11458;
  float val = (float)value;
  float xx = round((val / 10000) * 180);
  for (float i = 0; i <= xx; i++) {
    long x = round(sin(i * PI / 180) * 100);
    sum = sum + x;
  }
  return round((float)((100 * sum) / maxSin));
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print(F("Attempting MQTT connection..."));
    
    if (client.connect(myAddress)) {
      Serial.println("connected");

      char pub[30] = "";
      sprintf(pub, "{ \"address\":\"%s\" }", myAddress);
      client.publish("sporik/connect", pub);
      
      client.subscribe("sporik/register");
      delay(1000);
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(client.state());
      Serial.println(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup()
{
  Serial.begin(57600);

  client.setServer(server, port);
  client.setCallback(callback);

  Serial.println("INIT NET");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // initialize the Ethernet device not using DHCP:
    Ethernet.begin(mac, ip, myDns, gateway, subnet);

  }
  // Allow the hardware to sort itself out

  Serial.print("My IP address: ");
  ip = Ethernet.localIP();
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(ip[thisByte], DEC);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("Init interrupt");

  pinMode(inputZeroCrossPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(inputZeroCrossPin), setZeroCrossTime, RISING);
  pinMode(outputTogglePin, OUTPUT);
  digitalWrite(outputTogglePin, LOW);

  delay(300);


  Serial.println("Init timer");
  noInterrupts();           // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A = (20000 / 200) - 1;   // toggle after counting to 
  TCCR1A |= (1 << COM1A0);   // Toggle OC1A on Compare Match.
  TCCR1B |= (1 << WGM12);    // CTC mode
  TCCR1B |= (1 << CS11);     // clock on, pre-scaler
  TIMSK1 |= (1 << OCIE1A);
  interrupts();             // enable all interrupts
   
  delay(300);
}


void setZeroCrossTime() {
  elapsedTime = 0;
  if (regulationValue < 9900) digitalWrite(outputTogglePin, LOW); 
}


ISR(TIMER1_COMPA_vect)
{
  // increment elapsedTime (by 50us) until reach regulation value
  if (regulationValue < 100) return;
  
  elapsedTime = elapsedTime + 50;
  if (elapsedTime >= (10000 - regulationValue)) {
    digitalWrite(outputTogglePin, HIGH);
    return;
  }
}
// 10000 - 9944 = 56, 



void sendMeasurement() {
  Serial.println(F("doing measurement"));
  char pub[128];
  sprintf(pub, "{\"address\":\"%s\",\"value\":%i,\"r\":%i}", myAddress, measurementValue, parseTimeToPercent(regulationValue));
  client.publish("sporik/measurement", pub);
}


unsigned int loopCounter = 0;
unsigned int maxLoop = 10000;

void loop()
{

  if (!client.connected()) {
    reconnect();
  }
  if (loopCounter == maxLoop - 1) {
    measurementValue = analogRead(inputAnalogPin);
    loopCounter++;
  } else if (loopCounter == maxLoop) {
    // do measurement once in a time
    if (isRegistered) sendMeasurement();
    loopCounter = 0;
  } else {
    loopCounter++;
  }
  
  client.loop();

}
