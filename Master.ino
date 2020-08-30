#include <DHT.h>
#include <DHT_U.h>
#include <Bridge.h>

#include <BridgeServer.h>
#include <BridgeClient.h>

#include <Process.h>
#include <FileIO.h>

#define DHTPIN    2
#define DHTTYPE   DHT22 
#define STEPPER_PIN_1   2
#define STEPPER_PIN_2   3
#define STEPPER_PIN_3   4
#define STEPPER_PIN_4   5
#define STEPPER_PIN_5   6
#define STEPPER_PIN_6   7
#define STEPPER_PIN_7   8
#define WATERPUMP_PIN   9

//#define TEMP_IN   A2
#define MOIST_IN   A0
#define LEVEL_IN   A1

#define MIN_MOTOR_STEP_TIME   2
#define NORMAL_OPERATION_TIME   10
#define ONE_MINUTE_PAST_MOTOR_ON  30000 // 500 iteration (2 ms each) * 60 secs in a min
#define ONE_SECOND_MS   1000
float temp;
float moist;
int space=2;
int first_cycle=1;
long int time1=0;
long int time2=0;

int email_enabled;
int maxCalls = 4;

// The number of times e-mail notifications have been run so far in this sketch
int calls = 0;

int crossed_temp_thresh=0;
int crossed_moist_thresh=0;

float tThMax;
float tThMin;
float smThMax;
float smThMin;

float tMax=0;
float tMin=100;
float smMin=0;
float smMax=100;

 String dataString;
 String dataStringSMMax;
 String dataStringSMMin;
 String dataStringTMax;
 String dataStringTMin;

BridgeServer server;
 

typedef enum 
{
  A_CHECK_WATER_TANK = 0,
  A_WAIT_FOR_WATER_MOTOR_ON,
  A_WAIT_FOR_WATER_MOTOR_OFF,
  A_CHECK_FIELD_DATA
}working_cycle_t;

typedef enum 
{
  MAIL_STILL_EMPTY_TANK = 0,
  MAIL_TANK_OK_NOW,
  MAIL_TOO_HOT,
  MAIL_TOO_COLD,
  MAIL_TOO_MUCH_MOISTURE,
  TO_WATER,
  NOT_TO_WATER
}field_action_t;

typedef enum 
{
  STILL_EMPTY_TANK = 0,
  TANK_OK_NOW,
  TOO_HOT,
  TOO_COLD,
  TOO_MUCH_MOISTURE
}mail_type_t;

DHT_Unified dht(DHTPIN, DHTTYPE);
sensor_t sensor;
uint32_t min_delay_DHT_ms;


void setup() {

  dht.begin();
  dht.temperature().getSensor(&sensor);
  
  Serial.begin(9600);
  Bridge.begin();

  FileSystem.begin();
  FileSystem.remove("/mnt/sd/arduino/www/sensordata.txt");

  File confFileSpace = FileSystem.open("/mnt/sd/arduino/www/configurespace.txt", FILE_READ);
  space=confFileSpace.parseInt();
  confFileSpace.close();

  File confFileEmail = FileSystem.open("/mnt/sd/arduino/www/configureemail.txt", FILE_READ);
  email_enabled=confFileEmail.parseInt();
  confFileEmail.close();

  File confFileThresh = FileSystem.open("/mnt/sd/arduino/www/limit.txt", FILE_READ);

  tThMin=confFileThresh.parseFloat();
  tThMax=confFileThresh.parseFloat();
  smThMin=confFileThresh.parseFloat();
  smThMax=confFileThresh.parseFloat();


  confFileThresh.close();

  dataString += getTimeStamp();
  dataStringTMax=dataString;
  dataStringTMin=dataString;
  dataStringSMMin=dataString;
  dataStringSMMax=dataString;


/* Set delay between sensor readings based on sensor details.
 * This delay will be used to count iterations between readings,
 * while the microp. is working, not by putting it in stall for that time.
 */
  min_delay_DHT_ms = sensor.min_delay / 1000;  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(STEPPER_PIN_1, OUTPUT);
  pinMode(STEPPER_PIN_2, OUTPUT);
  pinMode(STEPPER_PIN_3, OUTPUT);
  pinMode(STEPPER_PIN_4, OUTPUT);
  pinMode(STEPPER_PIN_5, OUTPUT);
  pinMode(STEPPER_PIN_6, OUTPUT);
  pinMode(STEPPER_PIN_7, OUTPUT);
  pinMode(WATERPUMP_PIN, OUTPUT);
}

void loop() {

  BridgeClient client = server.accept();

  if (client) {
    // Process request
      String command = client.readStringUntil('/');


  // is "param" command?
  if (command == "param"){
    paramCommand(client);
    }
  if (command=="conf"){
    confCommand(client);
    }

    // Close connection and free resources.
    client.stop();
  }


  static working_cycle_t activity = A_CHECK_WATER_TANK;
  static uint8_t current_delay_ms = NORMAL_OPERATION_TIME;
  static boolean reset_dht_waiting_cycles = false;

  String dataString;
  dataString += getTimeStamp();
  
  switch(activity)
  {
    case A_CHECK_WATER_TANK:
    
    if(false == MinWaterLevelOk())
    {
      digitalWrite(WATERPUMP_PIN, LOW);
      current_delay_ms = MIN_MOTOR_STEP_TIME; 
      activity = A_WAIT_FOR_WATER_MOTOR_ON;
    }
    else if(true == MaxWaterLevelReached())
    {
      current_delay_ms = NORMAL_OPERATION_TIME; //motor off
      activity = A_CHECK_FIELD_DATA;
    }
    else
    {
      activity = A_CHECK_FIELD_DATA;
    }
    break;


    case A_WAIT_FOR_WATER_MOTOR_ON:

    static uint16_t alarm_counter = 0;

    if(true == MinWaterLevelOk())
    {
      alarm_counter = 0;
      reset_dht_waiting_cycles = true;  //to make sure to have the first value reading from the DHT sensor immediately
      activity = A_CHECK_FIELD_DATA;
    }
    else
    {
      MotorOn();
      if(ONE_MINUTE_PAST_MOTOR_ON == alarm_counter)
      {
        alarm_counter = 0;
        SendMail(STILL_EMPTY_TANK);
        current_delay_ms = NORMAL_OPERATION_TIME; //motor off
        activity = A_WAIT_FOR_WATER_MOTOR_OFF;
      }
      else
      {
        alarm_counter ++;
      }
    }
    break;


    case A_WAIT_FOR_WATER_MOTOR_OFF:  // case for the manual refill of the tank

    if(true == MinWaterLevelOk())
    {
      SendMail(TANK_OK_NOW);
      reset_dht_waiting_cycles = true;  //to make sure to have the first value reading from the DHT sensor immediately
      activity = A_CHECK_FIELD_DATA;
    }
    else
    {
      // nothing
    }
    break;


    case A_CHECK_FIELD_DATA:

    uint16_t moist;
    float temp;
    field_action_t field_action;

    temp = AcquireTemperature(current_delay_ms, reset_dht_waiting_cycles); 
    
    
    if(true == reset_dht_waiting_cycles)
    {
      reset_dht_waiting_cycles = false;
    }

    field_action = ElaborateData(temp, moist);
    TakeFieldAction(field_action);
    activity = A_CHECK_WATER_TANK;
    break;
  }
  temp = AcquireTemperature(current_delay_ms, reset_dht_waiting_cycles);
  if(temp>tMax){
      tMax=temp;
      dataStringTMax=dataString;
            }
    if(temp<tMin){
      tMin=temp;
      dataStringTMin=dataString;
            }

    moist = AcquireGroundMoisture();
    if(moist>smMax){
      smMax=moist;
      dataStringSMMax=dataString;
            }
    if(moist<smMin){
      smMin=moist;
      dataStringSMMin=dataString;
            }
    
    String tString=String(temp);
    String smString=String(moist);

    File dataFile = FileSystem.open("/mnt/sd/arduino/www/temphum.txt", FILE_APPEND);
  
      // if the file is available, write to it:
    if (dataFile) {
      dataFile.print(dataString);
      dataFile.print("  Temperature= ");
      dataFile.print(tString);
      dataFile.print("*C ");
      dataFile.print("SoilMoisture= ");
      dataFile.print(smString);
      dataFile.println("%");
      dataFile.close();  
  delay(current_delay_ms);
  GetPlantParameters();
}
if(email_enabled==1){ 
  if (temp<=tThMin || temp>=tThMax) {crossed_temp_thresh++;}
  if (moist<=smThMin || moist>=smThMax) {crossed_moist_thresh++;}
    
    if (calls < maxCalls) {
      if(crossed_temp_thresh == 3){
      Serial.println(F("Triggered"));
      runSendEmail(0x00);
      
      calls++;
      crossed_temp_thresh=0;}
      
      if(crossed_moist_thresh == 3){
        Serial.println(F("Triggered"));
      runSendEmail(0x01);
      calls++;
      crossed_moist_thresh=0;}
   
    }else {
      Serial.println("\nTriggered! Skipping to save smtp calls.");
    }
}
}


void GetPlantParameters(void)
{
  // website settings
}


float AcquireTemperature(uint8_t current_delay_ms, boolean reset_dht_waiting_cycles)
{
  float temp;
  sensors_event_t event;
  uint32_t total_waiting_cycles;
  static uint32_t sensor_cycle = 0;

  if(min_delay_DHT_ms <= ONE_SECOND_MS) //we don't need to acquire values faster than one per second
  {
    /* "* 2" is because the state machine normally bounces between the data checking
     *  and the water level checking, so this function is called every two iteractions
     */
    total_waiting_cycles = ONE_SECOND_MS / ((uint32_t)current_delay_ms * 2);  
  }  
  else
  {
    if(0 == min_delay_DHT_ms % ((uint32_t)current_delay_ms * 2))
    {
      total_waiting_cycles = min_delay_DHT_ms / ((uint32_t)current_delay_ms * 2);
    }  
    else
    {
      total_waiting_cycles = min_delay_DHT_ms / ((uint32_t)current_delay_ms * 2) + 1;
    }  
  } 
  if(true == reset_dht_waiting_cycles)
  {
    sensor_cycle = 0;
  }
  else
  {
    //nothing
  }
  if(0 == sensor_cycle)
  {
    dht.temperature().getEvent(&event);
    
    if (isnan(event.temperature))
    {
      //tell the website there was a problem with the sensor 
    }
    else
    {
      temp = event.temperature;
    }
    sensor_cycle ++;
  }  
  else
  {
    if(sensor_cycle < total_waiting_cycles - 1)
    {
      sensor_cycle ++;
    }
    else
    {
      sensor_cycle = 0;
    }
  }
  return temp;
}


uint16_t AcquireGroundMoisture(void)
{
  uint16_t moist;
  
  moist = analogRead(MOIST_IN); //* coefficient
  return moist;
}


field_action_t ElaborateData(unsigned int temp, unsigned int moist)
{
  
}


void TakeFieldAction(field_action_t action)
{
  switch(action)
  {
    case TO_WATER:
    
    digitalWrite(WATERPUMP_PIN, HIGH);
    break;


    case NOT_TO_WATER:
    
    digitalWrite(WATERPUMP_PIN, LOW);
    break;

    /*
     * more to be added
     */
  }
}


boolean MinWaterLevelOk(void)
{
  
}


boolean MaxWaterLevelReached(void)
{
  
}


void SendMail(mail_type_t mail_type)
{
  
}


void MotorOn(void)
{
  static uint8_t step = 0;
  
  switch(step)
  {
    case 0:
    
    digitalWrite(STEPPER_PIN_1, HIGH);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;


    case 1:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, HIGH);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;
      

    case 2:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, HIGH);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;

            
    case 3:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, HIGH);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;


    case 4:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, HIGH);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;      


    case 5:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, HIGH);
    digitalWrite(STEPPER_PIN_7, LOW);
    break;


    case 6:
    
    digitalWrite(STEPPER_PIN_1, LOW);
    digitalWrite(STEPPER_PIN_2, LOW);
    digitalWrite(STEPPER_PIN_3, LOW);
    digitalWrite(STEPPER_PIN_4, LOW);
    digitalWrite(STEPPER_PIN_5, LOW);
    digitalWrite(STEPPER_PIN_6, LOW);
    digitalWrite(STEPPER_PIN_7, HIGH);
    break;                  
  } 
  step ++;   
  if(step > 6)
  {
    step = 0;
  }
}

String getTimeStamp() {
  String result;
  Process time;
  // date is a command line utility to get the date and the time
  // in different formats depending on the additional parameter
  time.begin("date");
  time.addParameter("+%D-%T");  // parameters: D for the complete date mm/dd/yy
  //             T for the time hh:mm:ss
  time.run();  // run the command

  // read the output of the command
  while (time.available() > 0) {
    char c = time.read();
    if (c != '\n')
      result += c;
  }

  return result;
}


void paramCommand(BridgeClient client){
  
  String parameter = client.readStringUntil('\r'); // continues scanning the URI


 /*check if there were any reading errors from sensor*/
    
    if (isnan(moist) || isnan(temp)) {
    client.println("Failed to read from DHT sensor!");
    return;  }
    
  if (parameter == "all"){                  //display both temperature and soilmoisture
   client.print(F("Temperature= ("));
   client.print(temp);
   client.print(F(" +- 0.5)*C  "));
   
   client.print(F("SoilMoisture= ("));
   client.print(moist);
   client.println(F(" +- 2)%  "));
  }
  if (parameter == "temp"){ //display temperature only
   client.print(F("Temperature= ("));
   client.print(temp);
   client.println(F(" +- 0.5)*C  "));
  }
  if (parameter == "moist"){  //display soilmoisture only
   client.print(F("SoilMoisture= ("));
   client.print(moist);
   client.println(F(" +- 2)%  "));
  }
  
  
    if (parameter == "resetStat"){
      smMin=100;
      smMax=0;
      tMin=100;
      tMax=0;
      client.println(F("All Statistics have successfully been reset"));
    }
  
    if (parameter == "resetHistory"){
    FileSystem.remove("/mnt/sd/arduino/www/sensordata.txt");
    File dataFileReset = FileSystem.open("/mnt/sd/arduino/www/sensordata.txt", FILE_APPEND);  //re-create the file
    dataFileReset.close(); 
      client.println(F("History has successfully been reset"));
    }
  if(parameter=="stat"){
    client.print(F("Maximum Temperature= ("));
   client.print(tMax);
   client.print(F(" +- 0.5)*C  "));
   client.println(dataStringTMax);
   client.print(F("Minimum Temperature= ("));
   client.print(tMin);
   client.print(F(" +- 0.5)*C  "));
   client.println(dataStringTMin);
   client.print(F("Maximum SoilMoisture= ("));
   client.print(smMax);
   client.print(F(" +- 2)%  "));
   client.println(dataStringSMMax);
   client.print(F("Minimum SoilMoisture= ("));
   client.print(smMin);
   client.print(F(" +- 2)%  "));
   client.println(dataStringSMMin);
    }
  }

  void confCommand(BridgeClient client){
  String config_command=client.readStringUntil('/');
  float temptemp;
  float moisttemp;
 if(config_command=="email"){
  FileSystem.remove("/mnt/sd/arduino/www/configureemail.txt");
  File mail=FileSystem.open("/mnt/sd/arduino/www/configureemail.txt", FILE_WRITE);
  email_enabled=client.parseInt();
  mail.print(email_enabled);
  mail.close();
  if(email_enabled){
    calls=0;
  client.println(F("Email notification has been enabled"));
  }
  else{
    client.println(F("Email notification has been disabled"));
    }
  }
 if(config_command=="space"){
  FileSystem.remove("/mnt/sd/arduino/www/configurespace.txt");
  File spaceconf=FileSystem.open("/mnt/sd/arduino/www/configurespace.txt", FILE_APPEND);
  space=client.parseInt();
  spaceconf.print(space);
  spaceconf.close();
  client.println("Space between values has been set to "+String(space));
 }
if(config_command=="read"){
  String type_config=client.readStringUntil('\r');
  if(type_config=="space") {client.print(space);}
  if(type_config=="email") {client.print("Email: "+String(email_enabled));}
  if(type_config=="json"){
  client.println("{\"space\":\""+String(space)+"\",\"email\":\""+
                     String(email_enabled)+"\",\"tmin\":\""+
                     String(tThMin)+"\",\"tmax\":\""+
                     String(tThMax)+"\",\"smmin\":\""+
                     String(smThMin)+"\",\"smmax\":\""+
                     String(smThMax)+"\"}");
                     pinMode(13,OUTPUT);
                     digitalWrite(13,HIGH);
                     delay(50);
                     digitalWrite(13,LOW);
                     }
  }
if(config_command=="limit"){
  String type_limit=client.readStringUntil('/');
  if(type_limit=="tmin"){tThMin=client.parseFloat();}
  if(type_limit=="tmax"){tThMax=client.parseFloat();}
  if(type_limit=="smmin"){smThMin=client.parseFloat();}
  if(type_limit=="smmax"){smThMax=client.parseFloat();}

  // swap values if user sets lower limit grater then upper limit
  if(tThMin>tThMax){
    temptemp=tThMin;
    tThMin=tThMax;
    tThMax=temptemp;
  }
  if(smThMin>smThMax){
    moisttemp=smThMin;
    smThMin=smThMax;
    smThMax=moisttemp;
  }
  
  FileSystem.remove("/mnt/sd/arduino/www/limit.txt");
  File limitConf=FileSystem.open("/mnt/sd/arduino/www/limit.txt",FILE_WRITE);
  limitConf.print(tThMin);
  limitConf.print('\n');
  limitConf.print(tThMax);
  limitConf.print('\n');
  limitConf.print(smThMin);
  limitConf.print('\n');
  limitConf.print(smThMax);
  limitConf.close();
}
}

void runSendEmail(byte paramter) {
 
Serial.println("Running SendAnEmail...");
 


  Process p;
  p.begin("python");
  if(paramter==0x00){
  p.addParameter("/mnt/sda1/tempnot.py");}
  if(paramter==0x01){
    p.addParameter("/mnt/sda1/humnot.py");
    }
  p.run();
}

