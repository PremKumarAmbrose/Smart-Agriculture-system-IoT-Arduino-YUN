#include <DHT.h>
#include <DHT_U.h>
#include <Bridge.h>

#include <YunServer.h>
#include <YunClient.h>

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

YunServer server;
 

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
  dht.humidity().getSensor(&sensor);

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

  static working_cycle_t activity = A_CHECK_WATER_TANK;
  static uint8_t current_delay_ms = NORMAL_OPERATION_TIME;
  static boolean reset_dht_waiting_cycles = false;
  
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

    GetPlantParameters();
    temp = AcquireTemperature(current_delay_ms, reset_dht_waiting_cycles);
    humid= AcquireHumidity()
    if(true == reset_dht_waiting_cycles)
    {
      reset_dht_waiting_cycles = false;
    }
    moist = AcquireGroundMoisture();
    field_action = ElaborateData(temp, moist);
    TakeFieldAction(field_action);
    activity = A_CHECK_WATER_TANK;
    break;
  }  
  delay(current_delay_ms);
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
    dht.humidity().getEvent(&event);
    if (isnan(event.temperature)||isnan(event.temperature))
    {
      //tell the website there was a problem with the sensor 
    }
    else
    {
      temp = event.temperature;
      humid = event.humidity
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
  DHToutput={temp,humid}
  return DHToutput;
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

