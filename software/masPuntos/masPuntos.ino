#include <SPI.h>
#include <Wire.h>
#include <HttpClient.h>
#include <DS3231.h>
#include <b64.h>
#include <ArduinoJson.h>
#include "Ethernet_STM.h"
#include "src/SSPComs.h"


#define CREDIT 31 //PB15
#define COLLECT 30 //PB14
#define METER_IN 29 //PB13
#define METER_OUT 28 //PB12
#define CLOCKS_PER_SEC 1000
#define BUTTON1 PA0
#define BUTTON2 PA1
#define BUTTONDEBOUNCE 20
#define LED_BUILTIN 32

HardwareTimer *timer;
RTClib myRTC;
DateTime now;
EthernetClient c;
HttpClient http(c);

int32_t end_time, end_time_500;
unsigned long serial;
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
// Name of the server we want to connect to
const char kHostname[] = "725c5482-da88-47de-b3fd-2ae21dae06bb.mock.pstmn.io";
// Path to download (this is the bit after the hostname in the URL
// that you want to download
const char kPath[] = "/credits";
//const char kPath[] = "/meterin";
// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30*1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;
int err;
bool meterIn, meterOut, flag1, flag2, buttonFlag, previousState,collect;
uint32_t end_time_meter_in, end_time_meter_out, credits;
uint16_t creditIn, creditOut, incremento;
unsigned int previousPress;
char buffer[128];

SSP_COMMAND_SETUP ssp_setup;
SSP_POLL_DATA poll;

void setup() {
  // put your setup code here, to run once:
  Serial2.begin(115200); //for output message
  // init pins
  pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(LED_BUILTIN, HIGH);
  pinMode(27,OUTPUT); //PB11, STATUS LED
  pinMode(METER_IN,INPUT_PULLUP); //PA0, ENTRADA CONTADOR CREDITO
  pinMode(METER_OUT,INPUT_PULLUP); //PA1, ENTRADA CONTADOR CREDITO 2
  pinMode(COLLECT,OUTPUT); //PB14, SALIDA COLLECT
  //digitalWrite(COLLECT, HIGH);
  pinMode(CREDIT,OUTPUT); //PB15, SALIDA PULSOS CREDITO
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON1 ), button_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(METER_IN), MeterIn_ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(METER_OUT), MeterOut_ISR, RISING);
  
  flag1 = false;
  flag2 = false;
  meterIn = false;
  meterOut = false;
  buttonFlag = false;
  previousState = true;
  incremento = 0;

  Wire.begin();
    
  // start the Ethernet connection:
  Serial2.println("Starting Ethernet connection");
  while (Ethernet.begin(mac) != 1)    //It Gets the IP from the DHCP Server
  {
    Serial2.println("Error getting IP address via DHCP, trying again...");
    //delay(15000);
  
  } 
  //else // print your local IP address:
  //{
  Serial2.print("My IP address: ");
  for (byte thisByte = 0; thisByte < 4; thisByte++) 
  {
    // print the value of each byte of the IP address:
    Serial2.print(Ethernet.localIP()[thisByte], DEC);
    Serial2.print(".");  
  }
  Serial2.println();
  //}
  
  end_time = millis() + 5000;
  end_time_500 = millis() + 500;
  
  //runValidator();
}

void sendMoney(uint8_t);
void sendCollect();
void deserialization();
void serialization();

void loop() {
  // put your main code here, to run repeatedly:
  if (millis() > end_time)
  {
    /*Serial2.println("Test SSP Arduino");
    Serial2.print(myRTC.now().unixtime());
    Serial2.println("s");*/
    end_time = millis() + 5000;
  }
  if (millis() > end_time_500)
  {
    if (ssp_poll(ssp_setup,&poll) != SSP_RESPONSE_OK)
    {
        Serial2.println("SSP_POLL_ERROR\n");
        return;
    }
    ParsePoll(&poll);
    //digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    //digitalWrite(COLLECT, !digitalRead(COLLECT));
    //digitalWrite(CREDIT, !digitalRead(CREDIT));
    Serial2.println("Poll Validator");
    end_time_500 = millis() + 500;
  }

  if(flag1 && millis() > end_time_meter_in)
  {
    //digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    flag1 = false;
    //send data to DB. patch credit
    Serial2.print("Credito Ingresado: ");
    Serial2.println(creditIn);
    creditIn = 0;
  }
  if(flag2 && millis() > end_time_meter_out)
  {
    digitalWrite(PB11, !digitalRead(PB11));
    flag2 = false;
    //send data to DB. patch collect
    Serial2.print("Credito Cobrado: ");
    Serial2.println(creditOut);
    creditOut = 0;
  }
  if ((millis() - previousPress) > BUTTONDEBOUNCE/*buttonDebounce*/ && buttonFlag)
  {
    previousPress = millis();
    if (!digitalRead(BUTTON1) /*== LOW*/ && previousState)
    {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      consulta();
      deserialization();
      credits=1;
      if(credits > 0) 
      {
        sendMoney(credits);
        Serial2.println(credits);
      }
      if(collect) 
      {
        sendCollect();
      }
      credits = 0;
      //previousState = false;
      /*serialization();
      Serial2.println(buffer);
      update();*/
      
    }

    else if (digitalRead(BUTTON1) /*== HIGH*/ && !previousState)
    {
      previousState = true;
    }
    buttonFlag = 0;
    //digitalWrite(OPTO, LOW);
  }  
     
}


void ParsePoll(SSP_POLL_DATA * poll)
{
  int i;
  for (i = 0; i < poll->event_count; ++i)
  {
    switch(poll->events[i].event)
    {
    case SSP_POLL_RESET:
      Serial2.println("Unit Reset");
      break;
    case SSP_POLL_READ:
      if (poll->events[i].data > 0)
        Serial2.print("Note Read: ");
        Serial2.println(poll->events[i].data);
      break;
    case SSP_POLL_CREDIT:
      Serial2.println("Credit: ");
      Serial2.println(poll->events[i].data);
      break;
    case SSP_POLL_REJECTING:
      break;
    case SSP_POLL_REJECTED:
      Serial2.println("Note Rejected");
      break;
    case SSP_POLL_STACKING:
      break;
    case SSP_POLL_STACKED:
      Serial2.println("Stacked");
      break;
    case SSP_POLL_SAFE_JAM:
      Serial2.println("Safe Jam");
      break;
    case SSP_POLL_UNSAFE_JAM:
      Serial2.println("Unsafe Jam");
      break;
    case SSP_POLL_DISABLED:
      Serial2.println("DISABLED");
      break;
    case SSP_POLL_FRAUD_ATTEMPT:
      Serial2.print("Fraud Attempt: ");
      Serial2.println(poll->events[i].data);
      break;
    case SSP_POLL_STACKER_FULL:
      Serial2.println("Stacker Full");
      break;
    case SSP_POLL_CASH_BOX_REMOVED:
        Serial2.println("Cashbox Removed");
        break;
    case SSP_POLL_CASH_BOX_REPLACED:
        Serial2.println("Cashbox Replaced");
        break;
    case SSP_POLL_CLEARED_FROM_FRONT:
        Serial2.println("Cleared from front");
        break;
    case SSP_POLL_CLEARED_INTO_CASHBOX:
        Serial2.println("Cleared Into Cashbox");
        break;
    }
  }
}

 void runValidator()
{
  Serial1.begin(9600);
  //setup the required information
  ssp_setup.Timeout = 1000;
  ssp_setup.RetryLevel = 3;
  ssp_setup.SSPAddress = 0;//ssp_address default;
  ssp_setup.EncryptionStatus = NO_ENCRYPTION;
  //this is the goal
  if (ssp_sync(ssp_setup) != SSP_RESPONSE_OK)
  {
      Serial2.println("NO VALIDATOR FOUND");
  }
  else Serial2.println("Validator Found"); 
  
  if(ssp_get_serial(ssp_setup, &serial ) != SSP_RESPONSE_OK)
  {
      Serial2.println("NO VALIDATOR FOUND");
  }
  else
  {
    Serial2.print("Serial: ");
    Serial2.println(serial );
  }
  
  if(ssp_enable(ssp_setup) != SSP_RESPONSE_OK)
  {
      Serial2.println("NO VALIDATOR FOUND");
  }
  else
  {
    Serial2.println("Validator Enabled");
   }
   if(ssp_enable_higher_protocol_events(ssp_setup) != SSP_RESPONSE_OK)
  {
      Serial2.println("NO VALIDATOR FOUND");
  }
  else
  {
    Serial2.println("ssp_enable_higher_protocol_events ON");
   }
   if (ssp_set_inhibits(ssp_setup,0xFF,0xFF) != SSP_RESPONSE_OK)
 {
      Serial2.println("Inhibits Failed\n");
 }
 else
  {
    Serial2.println("ssp_set_inhibits ON");
   }
}

void consulta()
{
  
  err = 0;
  err = http.get(kHostname, kPath);//this method return 0 if there is not error 
  if (err == 0)
  {
    Serial2.println("startedRequest ok");

    err = http.responseStatusCode();
    if (err >= 0)
    {
      Serial2.print("Got status code: ");
      Serial2.println(err);
     

      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (HTTP_SUCCESS >= 0) //HTTP_SUCCESS = 0;
      {
        int bodyLen = http.contentLength();
        Serial2.print("Content length is: ");
        Serial2.println(bodyLen);
        Serial2.println();
        Serial2.println("Body returned follows:");
      
        // Now we've got to the body, so we can print it out
        unsigned long timeoutStart = millis();
        char c;
        // Whilst we haven't timed out & haven't reached the end of the body
      
        int retorno = http.read((uint8_t*)buffer, sizeof(buffer));
        Serial2.println(retorno);
        Serial2.println(buffer);
        //consulta();
      }
      else
      {
        Serial2.print("Failed to skip response headers: ");
        Serial2.println(err);
      }
    }
    else
    {    
      Serial2.print("Getting response failed: ");
      Serial2.println(err);
    }
  }
  else
  {
    Serial2.print("Connect failed: ");
    Serial2.println(err);
  }

  // Inside the brackets, 200 is the capacity of the memory pool in bytes.
  // Don't forget to change this value to match your JSON document.
  // Use arduinojson.org/v6/assistant to compute the capacity.
 
  http.stop();

  // And just stop, now that we've tried a download
}

void update()
{
  err = 0;
  err = http.put(kHostname, kPath);//this method return 0 if there is not error 
  //err = http.post(kHostname, kPath);//this method return 0 if there is not error 
  if (err == 0)
  {
    Serial2.println("startedRequest ok");
    err = http.responseStatusCode();
    if (err >= 0)
    {
      Serial2.print("Got status code: ");
      Serial2.println(err);
     
      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (err >= 0)
      { 
        if (http.available())
            {
              uint8_t response = http.write((uint8_t *)buffer,sizeof(buffer));
              memset(buffer,0,sizeof(buffer));
              int bodyLen = http.contentLength();
              if(bodyLen > 0)
              {
                Serial2.print("Content length is: ");
                Serial2.println(bodyLen);
                Serial2.println();
                Serial2.println("Body returned follows:");
                int retorno = http.read((uint8_t*)buffer, sizeof(buffer));
                Serial2.println(retorno);
                Serial2.println(buffer);
              }
            }
      }
      else
      {
        Serial2.print("Failed to skip response headers: ");
        Serial2.println(err);
      }
    }
    else
    {    
      Serial2.print("Getting response failed: ");
      Serial2.println(err);
    }
  }
  else
  {
    Serial2.print("Connect failed: ");
    Serial2.println(err);
  }
  http.stop();
}

void MeterIn_ISR()
{
  digitalWrite(PC13, !digitalRead(PC13));
  creditIn++;
  meterIn = true;
  flag1 =true;
  end_time_meter_in = millis() + CLOCKS_PER_SEC;
}

void MeterOut_ISR()
{
  digitalWrite(PB11, !digitalRead(PB11));
  creditOut++;
  meterOut = true;
  flag2 = true;
  end_time_meter_out = millis() + CLOCKS_PER_SEC;
}

void sendMoney(uint8_t creditValue)
{
  if(creditValue != 0)
  {
    for(uint8_t i = 0; i<creditValue; i++)
    {
      for(uint8_t j = 0; j<2; j++)
      {
        digitalWrite(CREDIT,!digitalRead(CREDIT));
        delay(25); //25ms
      }
    }
  }
}

void sendCollect()
{
  digitalWrite(COLLECT,!digitalRead(COLLECT));
  delay(25);
  digitalWrite(COLLECT,!digitalRead(COLLECT));
}

void button_ISR()
{
  buttonFlag = 1;
}

void deserialization()
{
  // Inside the brackets, 200 is the capacity of the memory pool in bytes.
  // Don't forget to change this value to match your JSON document.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  //StaticJsonDocument<64> doc;
  const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) +60;
  DynamicJsonDocument doc(capacity);
   // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, buffer);
  // Test if parsing succeeds.
  if (error)
  {
    Serial2.print(F("deserializeJson() failed: "));
    Serial2.println(error.f_str());
    return;
  }
  credits = doc["credits"];
  collect = doc["collect"];
  memset(buffer,0,sizeof(buffer));
}

void serialization()
{
  // Inside the brackets, 200 is the capacity of the memory pool in bytes.
  // Don't forget to change this value to match your JSON document.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<64> doc;
  memset(buffer,0,sizeof(buffer));
  //doc["meterin"] = 20;//creditIn;
  doc["userid"] = 69;
  doc["id"] = 1;
  doc["title"] = "carlos" ;
  doc["body"] = "this is the body of carlos";

  //const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) +60;
  //DynamicJsonDocument doc(capacity);
   // Deserialize the JSON document
  serializeJsonPretty(doc, buffer);  
  //serializeJson(doc, buffer);  
}
