#include <DHT.h>
#include <DHT_U.h>
#include <Bridge.h>
#include <BridgeServer.h>
#include <BridgeClient.h>
#include <Process.h>
#include <FileIO.h>


#define DHTPIN    2
#define DHTTYPE   DHT22 
#define GROUND_PUMP 5
#define WATERPUMP_PIN  10
#define MOIST_IN   A0
#define LEVEL_IN   A2

//For timers
#define NORMAL_OPERATION_TIME   10
#define ONE_MINUTE_PAST_PUMP_ON  6000 // 100 iteration (10 ms each) * 60 secs in a min
#define ONE_SECOND_MS   1000

BridgeServer server;

//System States
typedef enum 
{
  A_CHECK_WATER_TANK = 0,
  A_WAIT_FOR_TANK_PUMP_ON,
  A_WAIT_FOR_TANK_PUMP_OFF,
  A_TAKE_FIELD_ACTION
}working_cycle_t;

typedef enum 
{
  TO_WATER = 0,
  NOT_TO_WATER
}field_action_t;

typedef enum 
{
  TO_FILL_WATER=0,
  NOT_TO_FILL_WATER
}tankaction_t;

typedef enum 
{
  STILL_EMPTY_TANK = 0x03,
  TANK_OK_NOW = 0x04
}mail_type_t;




//Sensor readings
float temp;
float humi;
float moist;
int waterlevel;
const int AirValue = 620;   
const int WaterValue = 310;
sensor_t sensor;
uint32_t min_delay_DHT_ms;
DHT_Unified dht(DHTPIN, DHTTYPE);



// time strings
String dataString;
String dataStringHMax;
String dataStringHMin;
String dataStringTMax;
String dataStringTMin;
String dataStringMMax;
String dataStringMMin;
int space = 2;
int LogSpace = 10;
long int time1=0;
long int time2=0;
unsigned long lastMillis = 0;
unsigned long MillisEmptytank=0;
static boolean reset_dht_waiting_cycles = false;

//Threshold Values
float hThMax;
float hThMin;
float tThMax;
float tThMin;
float mThMax;
float mThMin;

float humiMax=0;
float humiMin=100;
float tempMax=0;
float tempMin=100;
float MoistMax=0;
float MoistMin=100;

int crossed_temp_thresh=0;
int crossed_humi_thresh=0;
int crossed_moist_thresh=0;


int email_OnOff;
int maxCalls = 4;
int calls = 0;

void setup() {

  Serial.begin(9600);
  
  //Bridge connection between microcontroller and microprocessor
  Bridge.begin(); 
  dht.begin();
  dht.temperature().getSensor(&sensor);
  
  // Listen for incoming connection
  server.listenOnLocalhost();
  server.begin();

  //Linux FileSystem Setup
  FileSystem.begin();
  FileSystem.remove("/mnt/sda1/www/Sensor_data.txt");
  FileSystem.remove("/mnt/sda1/www/log.csv");

  //Check configureemail.txt file to enable or disable email alerts
  File confFileEmail = FileSystem.open("/mnt/sda1/www/configureemail.txt", FILE_READ);
  email_OnOff = confFileEmail.parseInt();
  confFileEmail.close();
  
  // set the threshold values to be stored in limit.txt file
  File confFileThresh = FileSystem.open("/mnt/sda1/www/limit.txt", FILE_READ);
  tThMin = confFileThresh.parseFloat();
  tThMax = confFileThresh.parseFloat();
  hThMin = confFileThresh.parseFloat();
  hThMax = confFileThresh.parseFloat();
  mThMin = confFileThresh.parseFloat();
  mThMax = confFileThresh.parseFloat();
  confFileThresh.close();
  dataString += getTimeStamp();
  dataStringHMax = dataString;
  dataStringHMin = dataString;
  dataStringTMin = dataString;
  dataStringMMax = dataString;
  dataStringMMin = dataString;
  min_delay_DHT_ms = sensor.min_delay / 1000;  
  pinMode(GROUND_PUMP, OUTPUT);
  pinMode(WATERPUMP_PIN, OUTPUT);
}

void loop() {

  static working_cycle_t activity = A_CHECK_WATER_TANK;
  static uint8_t current_delay_ms = NORMAL_OPERATION_TIME;


  MonitorMode();
  
  BridgeClient client = server.accept();  // Get clients coming from server
  if (client)
  {
    String command = client.readStringUntil('/'); // Process request
    if (command == "conf")
    {  
      confCommand(client);
    }
    client.stop();    // Close connection and free resources.
  }

  switch(activity)
  {
    
    case A_CHECK_WATER_TANK:
    
    if(false == MinWaterLevelOk())
    {
      WaterPlants(NOT_TO_WATER);
      activity = A_WAIT_FOR_TANK_PUMP_ON;
    }
    else if(true == MaxWaterLevelReached())
    {
      FillTank(NOT_TO_FILL_WATER);
      activity = A_TAKE_FIELD_ACTION;
    }
    else
    {
      activity = A_TAKE_FIELD_ACTION;
    }
    break;


    case A_WAIT_FOR_TANK_PUMP_ON:

    static uint16_t alarm_counter = 0;

    if(true == MinWaterLevelOk())
    {
      alarm_counter = 0;
      reset_dht_waiting_cycles = true;  //to make sure to have the first value reading from the DHT sensor immediately
      activity = A_TAKE_FIELD_ACTION;
    }
    else
    {
      FillTank(TO_FILL_WATER);
      if((millis() - MillisEmptytank) >= 60000){
        MillisEmptytank= millis();  
        alarm_counter = 0;
        runPythonScript(STILL_EMPTY_TANK);
        FillTank(NOT_TO_FILL_WATER);
        activity = A_WAIT_FOR_TANK_PUMP_OFF;
      } 
    }
    break;


    case A_WAIT_FOR_TANK_PUMP_OFF:  // case for the manual refill of the tank

    if(true == MinWaterLevelOk())
    {
      runPythonScript(TANK_OK_NOW);
      Serial.print("sending email..Tank Ok");
      reset_dht_waiting_cycles = true;  //to make sure to have the first value reading from the DHT sensor immediately
      activity = A_TAKE_FIELD_ACTION;
    }
    else
    {
      // nothing
    }
    break;


    case A_TAKE_FIELD_ACTION:

    field_action_t field_action;

    ElaborateData(moist);
    activity = A_CHECK_WATER_TANK;
    break;
  }  
  delay(current_delay_ms);
}

void MonitorMode()
{
  String dataString;

  delay(50); // Poll every 50ms
  time2 = millis();
  temp = AcquireTemperature();
  humi = AcquireHumidity();
  moist = AcquireGroundMoisture();    
  waterlevel = AcquireWaterLevel();

  if((millis() - lastMillis) >= (LogSpace * 1000)) //for the presentation it has been set to 5 seconds
  {  
    AcquireLog();
    lastMillis =millis();
  } 
  if((time2 - time1) >= (space * 1000))
  {   
    dataString += getTimeStamp();
  
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it is a very slow sensor)
    time1 = millis();

    if(humi > humiMax)
    {
      humiMax = humi;
      dataStringHMax = dataString;
    }
    if(humi < humiMin)
    {
      humiMin = humi;
      dataStringHMin = dataString;
    }
    if(temp > tempMax)
    {
      tempMax = temp;
      dataStringTMax = dataString;
    }
    if(temp < tempMin)
    {
      tempMin = temp;
      dataStringTMin = dataString;
    }
    if(moist > MoistMax)
    {
      MoistMax = moist;
      dataStringMMax = dataString;
    }
    if(moist < MoistMin)
    {
      MoistMin = moist;
      dataStringMMin = dataString;
    }

    // send the e-mail (if needed)
    if(email_OnOff == 1)
    { 
      if (temp <= tThMin || temp>=tThMax)
      {
        crossed_temp_thresh ++;
      }
      if (humi <= hThMin || humi>=hThMax)
      {
        crossed_humi_thresh ++;
      }
      if (moist <= mThMin || moist >= mThMax)
      {
        crossed_moist_thresh++;
      }  
      if (calls < maxCalls)
      {
        if(crossed_temp_thresh == 3)
        {
          Serial.println(F("Triggered"));
          runPythonScript(0x00);
          
          calls++;
          crossed_temp_thresh = 0;
        }
        if(crossed_humi_thresh == 3)
        {
          Serial.println(F("Triggered"));
          runPythonScript(0x01);
          calls ++;
          crossed_humi_thresh = 0;
        }
        if(crossed_moist_thresh == 3)
        {
          Serial.println(F("Triggered"));
          runPythonScript(0x02);
          calls ++;
          crossed_moist_thresh = 0;
        }
      }
      else 
      {
        Serial.println("\nTriggered! Skipping to save smtp calls.");
      }
    }
  }
}

float AcquireTemperature(void)
{
  sensors_event_t event;

  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))
  {
    Serial.println(F("Error reading temperature!"));
  }
  else
  {
    temp = event.temperature;
  }
  return temp;
}


float AcquireHumidity(void)
{
  sensors_event_t event;

  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity))
  {
    Serial.println(F("Error reading humidity!"));
  }
  else
  {
    humi = event.relative_humidity;
  }
  return humi;
}


int AcquireWaterLevel(void)
{
  int level;

  level = analogRead(LEVEL_IN);
  int templevel = map(level, 330, 390, 0, 10);
  if (templevel >= 10)
  {
    level = 10;
  }
  else if(templevel <= 0)
  {
    level = 0;
  }
  else if(templevel > 0 && templevel < 10)
  {
    level = templevel;
  }
  level = level * 10;
  return level;
  
}

float AcquireGroundMoisture(void)
{
  float moistpercent;

  moistpercent = analogRead(MOIST_IN);
  moistpercent = map(moistpercent, AirValue, WaterValue, 0, 100);
  if(moistpercent >= 100)
  {
    moist = 100;
  }
  else if(moistpercent <= 0)
  {
    moist = 0;
  }
  else if(moistpercent > 0 && moistpercent < 100)
  {
    moist = moistpercent;
  }
  return moist;
}

void AcquireLog()
{
  String time = getTimeStamp();
  Serial.println(time);
  String tString = String(temp);
  String hString = String(humi);
  String mString = String(moist);
  File log = FileSystem.open("/mnt/sda1/www/log.csv", FILE_APPEND);
  if(log)
  {
    log.print('\n');
    log.print(time);
    log.print(',');
    log.print(temp);
    log.print(',');
    log.print(humi);
    log.print(',');
    int moisti = moist;
    log.print(moisti);
  }

  if((time=="22:59:55")||(time=="22:59:54"))
  {
    runPythonScript(0x06);
  }

  File dataFile = FileSystem.open("/mnt/sda1/www/Sensor_data.txt", FILE_APPEND);
  if (dataFile)
  {
    dataFile.print(time);
    dataFile.print("  Temperature= ");
    dataFile.print(tString);
    dataFile.print("*C ");
    dataFile.print("Humidity= ");
    dataFile.print(hString);
    dataFile.print("% ");
    dataFile.print("Moisture= ");
    dataFile.print(mString);
    dataFile.println("%");
    dataFile.close();   
  }
}


field_action_t ElaborateData(unsigned int moist)
{  
  if (moist <= mThMin)
  {
    WaterPlants(TO_WATER);
  }
  else if (moist >= mThMax)
  {
    WaterPlants(NOT_TO_WATER); 
  }
  else if (moist > mThMin && moist < mThMax)
  {
    WaterPlants(TO_WATER);
  }
}

void WaterPlants(field_action_t action)
{
  switch(action)
  {
    case TO_WATER:
    
    digitalWrite(WATERPUMP_PIN, HIGH);
    break;


    case NOT_TO_WATER:
    
    digitalWrite(WATERPUMP_PIN, LOW);
    break;
  }
}

void FillTank(tankaction_t tankaction)
{
  switch(tankaction)
  {
    case TO_FILL_WATER:
    
    digitalWrite(GROUND_PUMP, HIGH);
    break;


    case NOT_TO_FILL_WATER:
    
    digitalWrite(GROUND_PUMP, LOW);
    break;
  }
}


boolean MinWaterLevelOk(void)
{
  boolean ret = false;

  if(AcquireWaterLevel() >= 30)
  {
    ret = true;
  }
  return ret;
}


boolean MaxWaterLevelReached(void)
{
  boolean ret = false;

  if(AcquireWaterLevel() >= 100)
  {
    ret = true;
  }
  return ret;
}

void confCommand(BridgeClient client)
{
  String config_command = client.readStringUntil('/');
  float temptemp;
  float humtemp;
  float moisttemp;

  if(config_command == "email")
  {
    FileSystem.remove("/mnt/sda1/www/configureemail.txt");
    File mail = FileSystem.open("/mnt/sda1/www/configureemail.txt", FILE_WRITE);
    email_OnOff = client.parseInt();
    mail.print(email_OnOff);
    mail.close();
    if(email_OnOff)
    {
      calls = 0;
      Serial.println("Email notification has been enabled");
    }
    else
    {
      Serial.println("Email notification has been disabled");
    }
  }
  if(config_command == "limit")
  {
    String type_limit = client.readStringUntil('/');
    if(type_limit == "tmin")
    {
      tThMin = client.parseFloat();
    }
    if(type_limit == "tmax")
    {
      tThMax = client.parseFloat();
    }
    if(type_limit == "hmin")
    {
      hThMin = client.parseFloat();
    }
    if(type_limit == "hmax")
    {
      hThMax = client.parseFloat();
    }
    if(type_limit == "mmin")
    {
      mThMin = client.parseFloat();
    }
    if(type_limit == "mmax")
    {
      mThMax = client.parseFloat();
    }

    // swap values if user sets lower limit grater then upper limit
    if(tThMin > tThMax)
    {
      temptemp = tThMin;
      tThMin = tThMax;
      tThMax = temptemp;
    }
    if(hThMin > hThMax)
    {
      humtemp = hThMin;
      hThMin = hThMax;
      hThMax = humtemp;
    }
    if(mThMin > mThMax)
    {
      moisttemp = mThMin;
      mThMin = mThMax;
      mThMax = moisttemp;
    }
    FileSystem.remove("/mnt/sda1/www/limit.txt");
    File limitConf=FileSystem.open("/mnt/sda1/www/limit.txt", FILE_WRITE);
    limitConf.print(tThMin);
    limitConf.print('\n');
    limitConf.print(tThMax);
    limitConf.print('\n');
    limitConf.print(hThMin);
    limitConf.print('\n');
    limitConf.print(hThMax);
    limitConf.print('\n');
    limitConf.print(mThMin);
    limitConf.print('\n');
    limitConf.print(mThMax);
    limitConf.close();
  }

  if(config_command == "read")
  {
    String type_config = client.readStringUntil('\r');  
    if(type_config == "email")
    {
      client.print("Email: " + String(email_OnOff));
    }
    if (type_config == "sendlog")
    {
      runPythonScript(0x05);
    }
    if(type_config == "json")
    {
      client.println("{\"email\":\"" + String(email_OnOff) + "\",\"tmin\":\"" +
      String(tThMin) + "\",\"tmax\":\"" +
      String(tThMax) + "\",\"hmin\":\"" +
      String(hThMin) + "\",\"hmax\":\"" +
      String(hThMax) + "\",\"mmin\":\"" +
      String(mThMin) + "\",\"mmax\":\"" +
      String(mThMax) + "\",\"Temperature\":\"" +
      String(temp) + "\",\"Humidity\":\"" +
      String(humi) + "\",\"Moisture\":\"" +
      String(moist) + "\",\"WaterLevel\":\"" +
      String(waterlevel) + "\"}");
    }
  }

  

  if(config_command == "plant")
  {
    String type_plant = client.readStringUntil('\r');
    if(type_plant == "Tomato")
    {
      tThMin = 8.00;
      tThMax = 30.00;
      hThMin = 60.00;
      hThMax = 80.00;
      mThMin = 70.00;
      mThMax = 85.00;
    }
    if(type_plant == "Wheat")
    {
      tThMin = 16.00;
      tThMax = 30.00;
      hThMin = 60.00;
      hThMax = 80.00;
      mThMin = 55.00;
      mThMax = 100.00;
    }
    if(type_plant == "Rice")
    {
      tThMin = 20.00;
      tThMax = 35.00;
      hThMin = 60.00;
      hThMax = 80.00;
      mThMin = 70.00;
      mThMax = 100.00;
    }
    if(type_plant == "Potato")
    {
      tThMin = 15.00;
      tThMax = 30.00;
      hThMin = 60.00;
      hThMax = 80.00;
      mThMin = 80.00;
      mThMax = 95.00;
    }
    if(type_plant == "Grapes")
    {
      tThMin = 20.00;
      tThMax = 35.00;
      hThMin = 60.00;
      hThMax = 80.00;
      mThMin = 65.00;
      mThMax = 85.00;
    }
    
    FileSystem.remove("/mnt/sda1/www/limit.txt");
    File limitConf = FileSystem.open("/mnt/sda1/www/limit.txt", FILE_WRITE);
    limitConf.print(tThMin);
    limitConf.print('\n');
    limitConf.print(tThMax);
    limitConf.print('\n');
    limitConf.print(hThMin);
    limitConf.print('\n');
    limitConf.print(hThMax);
    limitConf.print('\n');
    limitConf.print(mThMin);
    limitConf.print('\n');
    limitConf.print(mThMax);
    limitConf.close();
  }
}

String getTimeStamp()
{
  String result;
  Process time;
  
  // date is a command line utility to get the date and the time
  // in different formats depending on the additional parameter
  time.begin("date");
  time.addParameter("+%T");  
                             // T for the time hh:mm:ss
  time.run();                // run the command

  // read the output of the command
  while (time.available() > 0)
  {
    char c = time.read();
    if (c != '\n')
      result += c;
  }
  return result;
}


void runPythonScript(byte paramter)
{
  Process p;
  p.begin("python");
  if(paramter == 0x00)
  {
    p.addParameter("/mnt/sda1/www/tempnot.py");
  }
  if(paramter == 0x01)
  {
    p.addParameter("/mnt/sda1/www/humnot.py");
  }
  if(paramter == 0x02)
  {
    p.addParameter("/mnt/sda1/www/moistnot.py");
  }
  if(paramter == 0x03)
  {
    p.addParameter("/mnt/sda1/www/TankEmpty.py");
  }
  if(paramter == 0x04)
  {
    p.addParameter("/mnt/sda1/www/TankOkNow.py");
  }
  if(paramter == 0x05)
  {
    p.addParameter("/mnt/sda1/www/logsend.py");    
  }
  if(paramter == 0x06)
  {
    p.addParameter("/mnt/sda1/www/deletelog.py");
  }    
  p.run();
}
