#include <Energia.h>
#include <RTChardware.h>
#include <Scheduler.h>
#include <LCD_GUI.h>
#include <Screen_K35.h>

#define MAXSCHEDULE 12

extern const uint8_t g_jobListImage[];
extern const uint8_t g_uncheckImage[];
extern const uint8_t g_checkImage[];

typedef enum command_t {
  ON = 1,
  OFF = 0
} cmd_type;

typedef struct condition {
  uint8_t cond_type;  // 0 - timebase, 1 - tempbase, 2 - range
  uint8_t minTemp, maxTemp;
  RTCTime time;
} condition;

typedef struct schedule {
  String childName;
  uint8_t childIndex;
  boolean enable = false;
  boolean done = false;
  cmd_type command;
  condition cond;
  listButton list;
  imageButton checkBox;
} schedule;

class Job {
  
  public:
  
    Job();
    uint8_t scheduleSize;
    boolean onLoop, onUpdate;
    schedule schedules[MAXSCHEDULE];
    Screen_K35* k35;
    
    void init(Screen_K35* _k35);   // 
    boolean addSchedule(String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t = 0, uint8_t max_t = 0);
    boolean removeSchedule(uint8_t index);
    boolean editSchedule(uint8_t i, String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t, uint8_t max_t);
    void setScheduleDetail(boolean add, uint8_t i, String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t, uint8_t max_t);
    void setJobEnable(uint8_t index, boolean flag);
    void setJobDone(uint8_t index, boolean flag);
    void setJobDoneTime(uint8_t index, RTCTime current);
    char* cmdTypeToString(cmd_type type);
    String timeToString(RTCTime t);
    boolean isEnable(uint8_t i);
  
  private:   
    
};
