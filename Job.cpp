#include "Job.h"

Job::Job() {
  ;
}

void Job::init(Screen_K35* _k35) {
  k35 = _k35;
  scheduleSize = 0;
  onLoop = true;
  onUpdate = false;
}

boolean Job::addSchedule(String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t, uint8_t max_t) {
  onUpdate = true;
  while (onLoop) {
    delay(1); 
  }
  if (!onLoop && scheduleSize < MAXSCHEDULE) {
    setScheduleDetail(true, scheduleSize++, name, index, cmd, cond_t, t, min_t, max_t);
    onUpdate = false;
    return true;
  }
  onUpdate = false;
  return false;
}

boolean Job::removeSchedule(uint8_t index) {
  onUpdate = true;
  while (onLoop) {
    delay(1); 
  }
  if (index == scheduleSize - 1) {
     ;
  } else {
    for (int i = 0; i < scheduleSize-1; i++) {
      if (i == index) {
        index++;
        schedules[i].childName = schedules[index].childName;
        schedules[i].childIndex = schedules[index].childIndex;
        schedules[i].command = schedules[index].command;
        schedules[i].done = schedules[index].done;
        schedules[i].cond.cond_type = schedules[index].cond.cond_type;
        schedules[i].cond.time.hour = schedules[index].cond.time.hour;
        schedules[i].cond.time.minute = schedules[index].cond.time.minute;
        schedules[i].cond.minTemp = schedules[index].cond.minTemp;
        schedules[i].cond.maxTemp = schedules[index].cond.maxTemp;
        schedules[i].list.define(k35, g_jobListImage, schedules[i].list.getX(), schedules[i].list.getY(), schedules[index].list.getText());
        schedules[i].list.enable();
        setJobEnable(i, schedules[index].enable);
      }
    }
  }
  scheduleSize--;
  onUpdate = false;
  return true;
}

boolean Job::editSchedule(uint8_t i, String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t, uint8_t max_t) {
  onUpdate = true;
  while (onLoop) {
    delay(1); 
  }
  if (!onLoop) {
    setScheduleDetail(false, i, name, index, cmd, cond_t, t, min_t, max_t);
    onUpdate = false;
    return true;
  }
  onUpdate = false;
  return false;
}

void Job::setScheduleDetail(boolean add, uint8_t i, String name, uint8_t index, cmd_type cmd, uint8_t cond_t, RTCTime t, uint8_t min_t, uint8_t max_t) {
  String text = "";
  schedules[i].childName = name;
  schedules[i].childIndex = index;
  schedules[i].command = cmd;
  schedules[i].cond.cond_type = cond_t;
  schedules[i].cond.minTemp = min_t;
  schedules[i].cond.maxTemp = max_t;
  if (cond_t == 0) {    // 0 - timebase, 1 - tempbase, 2 - range
    schedules[i].cond.time.hour = t.hour;
    schedules[i].cond.time.minute = t.minute;
    text = name + " " + String(cmdTypeToString(cmd)) + " at " + timeToString(t);
  } else if (cond_t == 1) {
    text = name + " " + String(cmdTypeToString(cmd)) + " at " + String(min_t) + (char)0xB0 + "F";
  } else if (cond_t == 2) {
    text = name + " ranged at " + String(min_t) + "-" + String(max_t);
  }
  if (add) {
    schedules[i].list.define(k35, g_jobListImage, 29, 72 + 24 * (i%6), text);
  } else {
    schedules[i].list.define(k35, g_jobListImage, 29, schedules[i].list.getY(), text);
  }  
  schedules[i].list.enable();
  schedules[i].enable = true;
  setJobEnable(i, schedules[i].enable);
}

void Job::setJobDone(uint8_t index, boolean flag) {
   schedules[index].done = flag;
}

void Job::setJobDoneTime(uint8_t index, RTCTime current) {
   schedules[index].cond.time.hour = current.hour;
   schedules[index].cond.time.minute = current.minute;
}

void Job::setJobEnable(uint8_t index, boolean flag) {
  schedules[index].enable = flag;
  if (flag) {
    schedules[index].checkBox.dDefine(k35, g_checkImage, 0, schedules[index].list.getY(), setItem(99, "CHECKBOX"));
  } else {
    schedules[index].checkBox.dDefine(k35, g_uncheckImage, 0, schedules[index].list.getY(), setItem(99, "CHECKBOX"));
  }
  schedules[index].checkBox.enable();
}

char* Job::cmdTypeToString(cmd_type type) {
  if (type == ON) return "ON";
  return "OFF"; 
}

String Job::timeToString(RTCTime t) {
  char s[] = "00:00";
  s[0] = t.hour / 10 + '0';
  s[1] = t.hour % 10 + '0';
  s[3] = t.minute / 10 + '0';
  s[4] = t.minute % 10 + '0';
  return String(s);
}

boolean Job::isEnable(uint8_t i) {
  return schedules[i].enable;
}
