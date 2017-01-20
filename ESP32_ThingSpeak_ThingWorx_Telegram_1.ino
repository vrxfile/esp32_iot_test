#include "DHT.h"
#include <BH1750FVI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP085_U.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <ArduinoJson.h>

//#include <ESP8266WiFi.h>
//#include <ESP8266TelegramBOT.h>

// Wi-Fi point
const char* ssid     = "MGBot";
const char* password = "Terminator812";

// For ThingSpeak IoT
const String CHANNELID_0 = "206369";
const String WRITEAPIKEY_0 = "0WTBXBB0D5FZUPWQ";
//IPAddress thingspeak_server(184, 106, 153, 149);
//IPAddress thingspeak_server(52, 203, 165, 232);
char thingspeak_server[] = "thingspeak.com";
const int httpPort = 80;

// For ThingWorx IoT
char iot_server[] = "cttit5402.cloud.thingworx.com";
IPAddress iot_address(52, 87, 101, 142);
char appKey[] = "ff9de221-e088-4726-97a6-c61aa61a15a6";
char thingName[] = "smile_flower_thing";
char serviceName[] = "apply_flower_data";

// ThingWorx parameters
#define sensorCount 9
char* sensorNames[] = {"ds18b20_temp", "dht11_temp", "dht11_hum", "bh1750_light", "bmp180_pressure", "bmp180_temp", "switch_button", "analog_moisture", "analog_light"};
float sensorValues[sensorCount];
// Номера датчиков
#define ds18b20_temp     0
#define dht11_temp       1
#define dht11_hum        2
#define bh1750_light     3
#define bmp180_pressure  4
#define bmp180_temp      5
#define switch_button    6
#define analog_moisture  7
#define analog_light     8

// Bot account
//#define BOTtoken "262485007:AAH_yU78u3nDcJf0R8JW3_hbzGKEv2_t2AE"
//#define BOTname "flower_bot"
//#define BOTusername "flower_iot_bot"
//TelegramBOT bot(BOTtoken, BOTname, BOTusername);

#define THINGSPEAK_UPDATE_TIME 15000   // Update ThingSpeak data server
#define THINGWORX_UPDATE_TIME 5000     // Update ThingWorx data server
#define SENSORS_UPDATE_TIME 5000       // Update time for all sensors
#define TELEGRAM_UPDATE_TIME 1000      // Update time for telegram bot

// DHT11 sensor
#define DHT11_PIN 33
DHT dht11(DHT11_PIN, DHT11, 15);

// DS18B20 sensor
#define ONE_WIRE_BUS 27
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// Switch button
#define BUTTON_PIN 34

// Analog moisture sensor
#define MOISTURE_PIN 12

// Analog light sensor
#define LIGHT_PIN 13

// BH1750 sensor
BH1750FVI LightSensor_1;

// BMP180 sensor
Adafruit_BMP085_Unified bmp180 = Adafruit_BMP085_Unified(10085);

// Relay
#define RELAY_PIN 32

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Control parameters
int relay_control = 0;

// Timer counters
unsigned long timer_thingspeak = 0;
unsigned long timer_thingworx = 0;
unsigned long timer_sensors = 0;
unsigned long timer_telegram = 0;

#define TIMEOUT 1000 // 1 second timout

// Максимальное время ожидания ответа от сервера
#define IOT_TIMEOUT1 5000
#define IOT_TIMEOUT2 100

// Таймер ожидания прихода символов с сервера
long timer_iot_timeout = 0;

// Размер приемного буффера
#define BUFF_LENGTH 256

// Приемный буфер
char buff[BUFF_LENGTH] = "";

// Main setup
void setup()
{
  // Init serial port
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Init Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Start bot library
  //  bot.begin();

  // Init actuators
  digitalWrite(RELAY_PIN, false);
  pinMode(RELAY_PIN, OUTPUT);

  // Init sensors
  pinMode(BUTTON_PIN, INPUT);
  //pinMode(MOISTURE_PIN, ANALOG);
  //pinMode(LIGHT_PIN, ANALOG);
  dht11.begin();
  ds18b20.begin();
  if (!bmp180.begin()) Serial.println("Could not find a valid BMP085 sensor!");
  LightSensor_1.begin();
  LightSensor_1.setMode(Continuously_High_Resolution_Mode);

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd_printstr("Hello!");
  lcd.setCursor(0, 1);
  lcd_printstr("I am ESP32");

  // First measurement and print data from sensors
  readDHT11();
  readDS18B20();
  readBH1750();
  readBMP180();
  readBUTTON();
  readMOISTURE();
  readLIGHT();
  printAllSensors();
}

// Main loop cycle
void loop()
{
  // Send data to ThingSpeak server
  if (millis() > timer_thingspeak + THINGSPEAK_UPDATE_TIME)
  {
    sendThingSpeakStream();
    timer_thingspeak = millis();
  }

  // Send data to ThingWorx server
  //  if (millis() > timer_thingworx + THINGWORX_UPDATE_TIME)
  //  {
  //    sendThingWorxStream();
  //    timer_thingworx = millis();
  //  }

  // Read all sensors
  if (millis() > timer_sensors + SENSORS_UPDATE_TIME)
  {
    readDHT11();
    readDS18B20();
    readBH1750();
    readBMP180();
    readBUTTON();
    readMOISTURE();
    readLIGHT();
    printAllSensors();
    timer_sensors = millis();
  }

  // Execute Telegram Bot
  //  if (millis() > timer_telegram + TELEGRAM_UPDATE_TIME)
  //  {
  //    bot.getUpdates(bot.message[0][1]);
  //    Telegram_ExecMessages();
  //    timer_telegram = millis();
  //  }
}

// Send IoT packet to ThingSpeak
void sendThingSpeakStream()
{
  WiFiClient client;
  Serial.print("Connecting to ");
  Serial.print(thingspeak_server);
  Serial.println("...");
  if (client.connect(thingspeak_server, httpPort))
  {
    if (client.connected())
    {
      Serial.println("Sending data to ThingSpeak server...\n");
      String post_data = "field1=";
      post_data = post_data + String(sensorValues[ds18b20_temp], 1);
      post_data = post_data + "&field2=";
      post_data = post_data + String(sensorValues[dht11_temp], 1);
      post_data = post_data + "&field3=";
      post_data = post_data + String(sensorValues[bmp180_temp], 1);
      post_data = post_data + "&field4=";
      post_data = post_data + String(sensorValues[dht11_hum], 1);
      post_data = post_data + "&field5=";
      post_data = post_data + String(sensorValues[bmp180_pressure], 1);
      post_data = post_data + "&field6=";
      post_data = post_data + String(sensorValues[bh1750_light], 1);
      post_data = post_data + "&field7=";
      post_data = post_data + String(sensorValues[analog_moisture], 1);
      post_data = post_data + "&field8=";
      post_data = post_data + String(sensorValues[analog_light], 1);
      Serial.println("Data to be send:");
      Serial.println(post_data);
      client.println("POST /update HTTP/1.1");
      client.println("Host: api.thingspeak.com");
      client.println("Connection: close");
      client.println("X-THINGSPEAKAPIKEY: " + WRITEAPIKEY_0);
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.print("Content-Length: ");
      int thisLength = post_data.length();
      client.println(thisLength);
      client.println();
      client.println(post_data);
      client.println();
      delay(1000);
      timer_thingspeak = millis();
      while ((client.available() == 0) && (millis() < timer_thingspeak + TIMEOUT));
      while (client.available() > 0)
      {
        char inData = client.read();
        Serial.print(inData);
      }
      Serial.println();
      client.stop();
    }
    Serial.println("Data sent OK!");
    Serial.println();
  }
}

// Подключение к серверу IoT ThingWorx
void sendThingWorxStream()
{
  WiFiClient client;
  // Подключение к серверу
  Serial.println("Connecting to IoT server...");
  if (client.connect(iot_address, 443))
  {
    // Проверка установления соединения
    if (client.connected())
    {
      // Отправка заголовка сетевого пакета
      Serial.println("Sending data to IoT server...\n");
      Serial.print("POST /Thingworx/Things/");
      client.print("POST /Thingworx/Things/");
      Serial.print(thingName);
      client.print(thingName);
      Serial.print("/Services/");
      client.print("/Services/");
      Serial.print(serviceName);
      client.print(serviceName);
      Serial.print("?appKey=");
      client.print("?appKey=");
      Serial.print(appKey);
      client.print(appKey);
      Serial.print("&method=post&x-thingworx-session=true");
      client.print("&method=post&x-thingworx-session=true");
      // Отправка данных с датчиков
      for (int idx = 0; idx < sensorCount; idx ++)
      {
        Serial.print("&");
        client.print("&");
        Serial.print(sensorNames[idx]);
        client.print(sensorNames[idx]);
        Serial.print("=");
        client.print("=");
        Serial.print(sensorValues[idx]);
        client.print(sensorValues[idx]);
      }
      // Закрываем пакет
      Serial.println(" HTTP/1.1");
      client.println(" HTTP/1.1");
      Serial.println("Accept: application/json");
      client.println("Accept: application/json");
      Serial.print("Host: ");
      client.print("Host: ");
      Serial.println(iot_server);
      client.println(iot_server);
      Serial.println("Content-Type: application/json");
      client.println("Content-Type: application/json");
      Serial.println();
      client.println();

      // Ждем ответа от сервера
      timer_iot_timeout = millis();
      while ((client.available() == 0) && (millis() < timer_iot_timeout + IOT_TIMEOUT1));

      // Выводим ответ о сервера, и, если медленное соединение, ждем выход по таймауту
      int iii = 0;
      bool currentLineIsBlank = true;
      bool flagJSON = false;
      timer_iot_timeout = millis();
      while ((millis() < timer_iot_timeout + IOT_TIMEOUT2) && (client.connected()))
      {
        while (client.available() > 0)
        {
          char symb = client.read();
          Serial.print(symb);
          if (symb == '{')
          {
            flagJSON = true;
          }
          else if (symb == '}')
          {
            flagJSON = false;
          }
          if (flagJSON == true)
          {
            buff[iii] = symb;
            iii ++;
          }
          timer_iot_timeout = millis();
        }
      }
      buff[iii] = '}';
      buff[iii + 1] = '\0';
      Serial.println(buff);
      // Закрываем соединение
      client.stop();

      // Расшифровываем параметры
      StaticJsonBuffer<BUFF_LENGTH> jsonBuffer;
      JsonObject& json_array = jsonBuffer.parseObject(buff);
      relay_control = json_array["relay_control"];
      Serial.println("Relay control:   " + String(relay_control));
      Serial.println();
      // Делаем управление устройствами
      if (relay_control)
      {
        digitalWrite(RELAY_PIN, true);
      }
      else
      {
        digitalWrite(RELAY_PIN, false);
      }
      Serial.println("Packet successfully sent!");
      Serial.println();
    }
  }
}

// Print sensors data to terminal
void printAllSensors()
{
  for (int i = 0; i < sensorCount; i++)
  {
    Serial.print(sensorNames[i]);
    Serial.print(" = ");
    Serial.println(sensorValues[i]);
  }
  Serial.print("Relay state: ");
  Serial.println(relay_control);
  Serial.println("");
  lcd.setCursor(0, 0);
  lcd_printstr("T = " + String(sensorValues[dht11_temp], 1) + " *C ");
  lcd.setCursor(0, 1);
  lcd_printstr("H = " + String(sensorValues[dht11_hum], 1) + " % ");
}

// Read DHT11 sensor
void readDHT11()
{
  sensorValues[dht11_hum] = dht11.readHumidity();
  sensorValues[dht11_temp] = dht11.readTemperature();
  if (isnan(sensorValues[dht11_hum]) || isnan(sensorValues[dht11_temp]))
  {
    Serial.println("Failed to read from DHT11 sensor!");
  }
}

// Read DS18B20 sensor
void readDS18B20()
{
  ds18b20.requestTemperatures();
  sensorValues[ds18b20_temp] = ds18b20.getTempCByIndex(0);
  if (isnan(sensorValues[ds18b20_temp]))
  {
    Serial.println("Failed to read from DS18B20 sensor!");
  }
}

// Read BH1750 sensor
void readBH1750()
{
  sensorValues[bh1750_light] = LightSensor_1.getAmbientLight();
}

// Read BMP180 sensor
void readBMP180()
{
  float t3 = 0;
  sensors_event_t p_event;
  bmp180.getEvent(&p_event);
  if (p_event.pressure)
  {
    sensorValues[bmp180_pressure] = p_event.pressure * 7.5006 / 10;
    bmp180.getTemperature(&t3);
  }
  sensorValues[bmp180_temp] = t3;
}

// Read BUTTON switch
void readBUTTON()
{
  sensorValues[switch_button] = digitalRead(BUTTON_PIN);
}

// Read analog moisture sensor
void readMOISTURE()
{
  sensorValues[analog_moisture] = analogRead(12);
}

// Read analog light sensor
void readLIGHT()
{
  sensorValues[analog_light] = analogRead(LIGHT_PIN);
}

// Execute Telegram events
/*
  void Telegram_ExecMessages()
  {
  for (int i = 1; i < bot.message[0][0].toInt() + 1; i++)
  {
    bot.message[i][5] = bot.message[i][5].substring(0, bot.message[i][5].length());
    String str1 = bot.message[i][5];
    str1.toUpperCase();
    Serial.println("Message: " + str1);
    if ((str1 == "START") || (str1 == "\/START"))
    {
      bot.sendMessage(bot.message[i][4], "Привет! Я цветочек!", "");
    }
    else if ((str1 == "STOP") || (str1 == "\/STOP"))
    {
      bot.sendMessage(bot.message[i][4], "Пока!", "");
    }
    else if ((str1 == "AIR") || (str1 == "WEATHER"))
    {
      bot.sendMessage(bot.message[i][4], "Температура воздуха: " + String(t1, 1) + " *C", "");
      bot.sendMessage(bot.message[i][4], "Влажность воздуха: " + String(h1, 1) + " %", "");
    }
    else if ((str1 == "AIR TEMP") || (str1 == "AIR TEMPERATURE"))
    {
      bot.sendMessage(bot.message[i][4], "Температура воздуха: " + String(t1, 1) + " *C", "");
    }
    else if ((str1 == "AIR HUM") || (str1 == "AIR HUMIDITY"))
    {
      bot.sendMessage(bot.message[i][4], "Влажность воздуха: " + String(h1, 1) + " %", "");
    }
    else if ((str1 == "SOIL") || (str1 == "GROUND"))
    {
      bot.sendMessage(bot.message[i][4], "Температура почвы: " + String(t2, 1) + " *C", "");
      bot.sendMessage(bot.message[i][4], "Влажность почвы: " + String(m1, 1) + " %", "");
    }
    else if ((str1 == "SOIL TEMP") || (str1 == "SOIL TEMPERATURE"))
    {
      bot.sendMessage(bot.message[i][4], "Температура почвы: " + String(t2, 1) + " *C", "");
    }
    else if ((str1 == "SOIL HUM") || (str1 == "SOIL HUMIDITY") || (str1 == "SOIL MOISTURE"))
    {
      bot.sendMessage(bot.message[i][4], "Влажность почвы: " + String(m1, 1) + " %", "");
    }
    else if ((str1 == "LIGHT") || (str1 == "LUMINOSITY") || (str1 == "BRIGHT") || (str1 == "BRIGHTNESS"))
    {
      bot.sendMessage(bot.message[i][4], "Освещенность: " + String(l1, 1) + " lx", "");
    }
    else if ((str1 == "HI") || (str1 == "HI!") || (str1 == "HELLO") || (str1 == "HELLO!"))
    {
      bot.sendMessage(bot.message[i][4], "Привет! Я цветочек!", "");
    }
    else if ((str1 == "BYE") || (str1 == "BYE!") || (str1 == "BYE-BYE") || (str1 == "BYE-BYE!"))
    {
      bot.sendMessage(bot.message[i][4], "Пока!", "");
    }
    else if ((str1 == "HOW DO YOU DO") || (str1 == "HOW DO YOU DO?") || (str1 == "HOW ARE YOU") || (str1 == "HOW ARE YOU?")
             || (str1 == "HRU") || (str1 == "HRU?"))
    {
      if (m1 < MIN_MOISTURE)
      {
        bot.sendMessage(bot.message[i][4], "Я уже засох, поливай быстрей!", "");
      } else if ((m1 >= MIN_MOISTURE) && (m1 < AVG_MOISTURE))
      {
        bot.sendMessage(bot.message[i][4], "Ну желательно бы чуть-чуть полить", "");
      } else if ((m1 >= AVG_MOISTURE) && (m1 < MAX_MOISTURE))
      {
        bot.sendMessage(bot.message[i][4], "У меня всё нормально, а у тебя?", "");
      } else if (m1 >= MAX_MOISTURE)
      {
        bot.sendMessage(bot.message[i][4], "Меня залили, давай вытирай подоконник!", "");
      }
    }
    else if ((str1 == "WHERE ARE YOU") || (str1 == "WHERE R U") || (str1 == "WHERE ARE YOU?") || (str1 == "WHERE R U?"))
    {
      bot.sendMessage(bot.message[i][4], "Привет, я в Ташкенте на M2M КОНФЕРЕНЦИЯ UCELL \"Технологии будущего\"", "");
    }

    else if ((str1 == "YOU LIKE IT") || (str1 == "U LIKE IT") || (str1 == "YOU LIKE IT?") || (str1 == "U LIKE IT?"))
    {
      bot.sendMessage(bot.message[i][4], "Мне нравится Ташкент :)", "");
    }

    else if ((str1 == "WHAT IS IOT") || (str1 == "IOT") || (str1 == "WHAT IS IOT?") || (str1 == "IOT?"))
    {
      bot.sendMessage(bot.message[i][4], "Интернет вещей — концепция  вычислительной сети физических предметов («вещей»), оснащённых встроенными технологиями для взаимодействия друг с другом или с внешней средой, рассматривающая организацию таких сетей как явление, способное перестроить экономические и общественные процессы, исключающее из части действий и операций необходимость участия человека", "");
    }
    else if ((str1 == ":(") || (str1 == ":-("))
    {
      smile_type = 1;
      bot.sendMessage(bot.message[i][4], "Чё такой грустный?", "");
    }
    else if ((str1 == ":)") || (str1 == ":-)"))
    {
      smile_type = 2;
      bot.sendMessage(bot.message[i][4], "С чего радуемся?", "");
    }
    else if (str1 == "BLINK")
    {
      smile_type = 3;
      bot.sendMessage(bot.message[i][4], "Ну ладно, помигаем", "");
    }
    else if ((str1 == "H2O") || (str1 == "WATER") || (str1 == "PUMP"))
    {
      bot.sendMessage(bot.message[i][4], "Поливаем 5 секунд...", "");
      digitalWrite(RELAY_PIN, true);
      delay(5000);
      digitalWrite(RELAY_PIN, false);
      pump1 = 1;
      bot.sendMessage(bot.message[i][4], "Всё, цветок полит!", "");
    }
    else
    {
      if (random(3) == 0)
      {
        bot.sendMessage(bot.message[i][4], "Чё?", "");
      }
      else if (random(3) == 1)
      {
        bot.sendMessage(bot.message[i][4], "Не понял", "");
      }
      else
      {
        bot.sendMessage(bot.message[i][4], "Не знаю такого", "");
      }
    }
  }
  bot.message[0][0] = "";
  }
*/

// Print string to I2C LCD
void lcd_printstr(String str1)
{
  for (int i = 0; i < str1.length(); i++)
  {
    lcd.print(str1.charAt(i));
  }
}

