#include <SPI.h>
#include <Scheduler.h>
#include <RTChardware.h>
#include <AIR430BoostFCC.h>
#include <Screen_K35.h>
#include <LCD_graphics.h>
#include <LCD_GUI.h>
#include "util.h"
#include "Keyboard.h"

#define DEBUG true
#define ADDRESS_MASTER 0x00
#define ADDRESS_LOCAL 0x10  /* Every slave controller increments by 0x10, max total 4 controllers, so node up to 0x30 */
#define TYPE SLAVE
struct sPacket rxPacket;
struct sPacket txPacket;
struct mPacket mxPacket;
struct cPacket cxPacket;
struct jPacket jxPacket;
struct kPacket kxPacket;

// UI related elements
Screen_K35 myScreen;
button homeButton, updateTimeModeToggle, roomNameButton, roomTempButton;
imageButton optionButton, nextButton, returnButton, updateButton, removeButton, onButton, offButton, jobButton, setTimeButton, roomRenameButton;
imageButton addJobButton, checkButton, uncheckButton, previousButton, returnsButton, childInfoButton, removeJobButton;
imageButton timeCheckButton, minTempCheckButton, maxTempCheckButton, onCheckButton, offCheckButton;
imageButton hourPlusButton, minPlusButton, minTempPlusButton, maxTempPlusButton;
imageButton hourMinusButton, minMinusButton, minTempMinusButton, maxTempMinusButton;
item home_i, option_i;

// System main vars
String masterName;
boolean firstInitialized = false, celsius = true, connectedToMaster = false;
boolean timeInterrupt = false, _idle = false, _idleInterrupt = false, _listening = true, _listenInterrupt = false;
roomStruct thisRoom;
uint8_t roomSize = 0;
uint8_t updateTimeMode;
long _timer = 0;

void setup()
{
  // start serial communication at 115200 baud
  Serial.begin(115200);
  pinMode(STATUS, OUTPUT);
  digitalWrite(STATUS, LOW);
  initAIR();
  initLCD();
  firstInitialized = firstInit();
  pinMode(PUSH2, INPUT_PULLUP);
  attachInterrupt(PUSH2, softReset, FALLING);
  Scheduler.startLoop(updateCurrentRoomTemp);
  Scheduler.startLoop(idle);
  Scheduler.startLoop(jobs);
  Scheduler.startLoop(listening);
  roomConfig(&thisRoom);
}

void loop()
{
  // put your main code here, to run repeatedly:
  // DO nothing
}

/********************************************************************************/
/*                               T2T Communication                              */
/********************************************************************************/
void listening() {
  while(_listenInterrupt) {delay(1);}
  while(Radio.busy()){}
  _listening = true;
  if (Radio.receiverOn((unsigned char*)&mxPacket, sizeof(mxPacket), 1000) > 0 && mxPacket.master == ADDRESS_MASTER) {
    if (!strcmp((char*)mxPacket.msg, "PAIR") && !connectedToMaster) {
      while(Radio.busy()){}
      setRoomBasicInfo();     
      feedback("ACK");
    } else if (mxPacket.node == ADDRESS_LOCAL) {
      if (!strcmp((char*)mxPacket.msg, "CONNECT") && !connectedToMaster) {
        masterName = (char*)mxPacket.name;
        connectedToMaster = true;
        digitalWrite(STATUS, HIGH);  // turn on indicator
        setRoomBasicInfo();
        feedback("ACK");  // Send ACK
      } else if (!strcmp((char*)mxPacket.msg, "MNAME") && connectedToMaster) {
        masterName = (char*)mxPacket.name;
        feedback("ACK");  // Send ACK
      } else if (!strcmp((char*)mxPacket.msg, "NAME") && connectedToMaster) {
        thisRoom.name = (char*)mxPacket.name;
        feedback("ACK");  // Send ACK
      } else if (!strcmp((char*)mxPacket.msg, "DEL") && connectedToMaster) {
        softReset();
        feedback("ACK");  // Send ACK
      } else if (!strcmp((char*)mxPacket.msg, "BASIC") && connectedToMaster) {
        setRoomBasicInfo();
        feedback("BASIC");  // Send basic info
      } else if (!strcmp((char*)mxPacket.msg, "CHILD") && connectedToMaster) {
        feedback("CHILD");
        for (int i = 0; i < thisRoom.childSize; i++) {
          thisRoom.childList[i].name.toCharArray((char*)cxPacket.name[i], 8);
          cxPacket.node[i] = thisRoom.childList[i].node;
          cxPacket.type[i] = (uint8_t) thisRoom.childList[i].type;
        }
        strcpy((char*)cxPacket.msg, "CHILD");
        Radio.transmit(ADDRESS_MASTER, (unsigned char*)&cxPacket, sizeof(cxPacket));  // send children info
      } else if (!strcmp((char*)mxPacket.msg, "CTEMP") && connectedToMaster) {
        mxPacket.tempC = getChildTemp(thisRoom.childList[mxPacket.tempC]);
        feedback("ACK");  // Send ACK
      } else if (!strcmp((char*)mxPacket.msg, "JOB") && connectedToMaster) {
        feedback("JOB");
        feedbackJobInfo();
      } else if (!strcmp((char*)mxPacket.msg, "ADDJOB") && connectedToMaster) {
        feedback("ACK");
        listenForJobInfo(true, 0xff);
        feedbackJobInfo();
      } else if (!strcmp((char*)mxPacket.msg, "EDITJOB") && connectedToMaster) {
        uint8_t jobIndex = mxPacket.data;
        feedback("ACK");
        listenForJobInfo(false, jobIndex);
        feedbackJobInfo();
      } else if (!strcmp((char*)mxPacket.msg, "DELJOB") && connectedToMaster) {
        feedback("ACK");
        thisRoom.job.removeSchedule(mxPacket.data);
        feedbackJobInfo();
      } else if (!strcmp((char*)mxPacket.msg, "ENJOB") && connectedToMaster) {
        feedback("ACK");
        thisRoom.job.setJobEnable(mxPacket.data, !thisRoom.job.isEnable(mxPacket.data));
      } else if (!strcmp((char*)mxPacket.msg, "DCHD") && connectedToMaster) {  // delete child
        feedback("ACK");  // Send ACK
        removeChild(&thisRoom, mxPacket.tempC);        
      } else {
        feedback("NAK");  // send nak
      }
    } 
  }
  _listening = false;
  delay(1);
}

void listenForJobInfo(boolean add, uint8_t index) {
  if (Radio.receiverOn((unsigned char*)&jxPacket, sizeof(jxPacket), 30000) > 0) {
    String name = thisRoom.childList[jxPacket.childIndex[0]].name;
    uint8_t ci = jxPacket.childIndex[0];
    cmd_type cmd = (cmd_type) jxPacket.cmd[0];
    uint8_t condition = jxPacket.cond[0];
    RTCTime t;
    uint8_t tmin, tmax;
    if (condition == 0) {
      t.hour = jxPacket.data1[0];
      t.minute = jxPacket.data2[0];
      tmin = 72;
      tmax = 76;
    } else {
      t.hour = 0;
      t.minute = 0;
      tmin = jxPacket.data1[0];
      tmax = jxPacket.data2[0];
    }
    if (add) thisRoom.job.addSchedule(name, ci, cmd, condition, t, tmin, tmax);
    else thisRoom.job.editSchedule(index, name, ci, cmd, condition, t, tmin, tmax);
  }
}

void feedbackJobInfo() {
  setJobInfo();
  Radio.transmit(ADDRESS_MASTER, (unsigned char*)&jxPacket, sizeof(jxPacket));  // send job info
  while(Radio.busy());
}

void setJobInfo() {
  kxPacket.childSize = thisRoom.job.scheduleSize;
  for (int i = 0; i < thisRoom.job.scheduleSize; i++) {
    if (thisRoom.job.isEnable(i))
      kxPacket.enable[i] = 1;
    else
      kxPacket.enable[i] = 0;
  }
  strcpy((char*)kxPacket.msg, "JOB");
  Radio.transmit(ADDRESS_MASTER, (unsigned char*)&kxPacket, sizeof(kxPacket));
  for (int i = 0; i < thisRoom.job.scheduleSize; i++) {
    jxPacket.childIndex[i] = thisRoom.job.schedules[i].childIndex;
    jxPacket.cond[i] = thisRoom.job.schedules[i].cond.cond_type;
    jxPacket.cmd[i] =  (uint8_t) thisRoom.job.schedules[i].command;
    if (thisRoom.job.schedules[i].cond.cond_type == 0) {
      jxPacket.data1[i] = thisRoom.job.schedules[i].cond.time.hour;
      jxPacket.data2[i] = thisRoom.job.schedules[i].cond.time.minute;
    } else {
      jxPacket.data1[i] = thisRoom.job.schedules[i].cond.minTemp;
      jxPacket.data2[i] = thisRoom.job.schedules[i].cond.maxTemp;
    }
  }
  while(Radio.busy()){}
  delay(100);
}

void setRoomBasicInfo() {
  thisRoom.name.toCharArray((char*)mxPacket.name, 10);
  mxPacket.master = ADDRESS_MASTER;
  mxPacket.node = ADDRESS_LOCAL;
  mxPacket.data = thisRoom.childSize;
  mxPacket.tempF = thisRoom.roomTempF;
  mxPacket.tempC = thisRoom.roomTempC;
}

// for Master-to-Slave communication only
void feedback(char* msg) {
  delay(100);
  strcpy((char*)mxPacket.msg, msg);
  Radio.transmit(ADDRESS_MASTER, (unsigned char*)&mxPacket, sizeof(mxPacket));
  while(Radio.busy()){}
}

/********************************************************************************/
/*                                  Job Controls                                */
/********************************************************************************/
void jobs() {  
  if (!thisRoom.job.onUpdate && thisRoom.job.scheduleSize > 0) {
    thisRoom.job.onLoop = true;
    RTCTime current;
    for (int i = 0 ; i < thisRoom.job.scheduleSize; i++) {
      if (thisRoom.job.schedules[i].enable) {
        RTC.GetAll(&current);
        if (thisRoom.job.schedules[i].cond.cond_type == 0) {  //Time base          
          if (!thisRoom.job.schedules[i].done && current.hour == thisRoom.job.schedules[i].cond.time.hour && current.minute == thisRoom.job.schedules[i].cond.time.minute/* && abs(current.second - thisRoom.job.schedules[i].cond.time.second) <= 1000*/) {
            childCommand(thisRoom.childList[thisRoom.job.schedules[i].childIndex], thisRoom.job.cmdTypeToString(thisRoom.job.schedules[i].command));
            thisRoom.job.setJobDone(i, true);
          } else if (thisRoom.job.schedules[i].done && abs(current.minute - thisRoom.job.schedules[i].cond.time.minute) > 0) {
            thisRoom.job.setJobDone(i, false);
          }
        } else if (thisRoom.job.schedules[i].cond.cond_type == 1) {  //Temp base
          if (!thisRoom.job.schedules[i].done && thisRoom.job.schedules[i].cond.minTemp == thisRoom.roomTempF) {
            childCommand(thisRoom.childList[thisRoom.job.schedules[i].childIndex], thisRoom.job.cmdTypeToString(thisRoom.job.schedules[i].command));
            thisRoom.job.setJobDoneTime(i, current);
            thisRoom.job.setJobDone(i, true);
          } else {
            if (abs(current.minute - thisRoom.job.schedules[i].cond.time.minute) >= 2) {
              thisRoom.job.setJobDone(i, false);
            }
          }
        } else if (thisRoom.job.schedules[i].cond.cond_type == 2) {  //range
          if (!thisRoom.job.schedules[i].done) {
            debug(String(thisRoom.roomTempF) + " " + thisRoom.job.schedules[i].cond.minTemp + " " + thisRoom.job.schedules[i].cond.maxTemp);
            if (thisRoom.roomTempF <= thisRoom.job.schedules[i].cond.minTemp) {
              childCommand(thisRoom.childList[thisRoom.job.schedules[i].childIndex], thisRoom.job.cmdTypeToString(OFF));
            } else if (thisRoom.roomTempF >= thisRoom.job.schedules[i].cond.maxTemp) {
              childCommand(thisRoom.childList[thisRoom.job.schedules[i].childIndex], thisRoom.job.cmdTypeToString(ON));
            }
            thisRoom.job.setJobDoneTime(i, current);
            thisRoom.job.setJobDone(i, true);
          } else {
            if (abs(current.minute - thisRoom.job.schedules[i].cond.time.minute) >= 1) {
              thisRoom.job.setJobDone(i, false);
            }
          }
        }
      }
    }
  } else {
    delay(5); 
  }
  thisRoom.job.onLoop = false;
  delay(5);
}

boolean jobConfig(roomStruct* room) {
  uint8_t page = 1;
  resetJobConfigUI(room, page, room->job.scheduleSize > 6);
  
  while (1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetJobConfigUI(room, page, room->job.scheduleSize > 6);
    }
    if (homeButton.isPressed()) {
      return HOME; 
    }
    if (returnsButton.check(true)) {
      return RETURN; 
    }
    for (int i = 0; i < room->job.scheduleSize; i++) {
      if (room->job.schedules[i].list.check(true)) {
        if (addJob(room, false, i)) return HOME;
        resetJobConfigUI(room, page, room->job.scheduleSize > 6);
      }
      if (room->job.schedules[i].checkBox.check(true)) {
        room->job.setJobEnable(i, !room->job.isEnable(i));
        resetJobConfigUI(room, page, room->job.scheduleSize > 6);
      }      
    }
    if (addJobButton.check(true)) {
      if (room->childSize > 0 && room->job.scheduleSize < MAXSCHEDULE && addJob(room, true, -1)) return HOME;
      resetJobConfigUI(room, page, room->job.scheduleSize > 6);
    }
    if (nextButton.check(true)) {
      resetJobConfigUI(room, ++page, room->job.scheduleSize > 6);
    }
    if (previousButton.check(true)) {
      resetJobConfigUI(room, --page, room->job.scheduleSize > 6);
    }
  }
}

boolean addJob(roomStruct* room, boolean add, uint8_t index) {
  setIdleInterrupt(true);
  if (add) debug("ADD JOB");
  else debug("EDIT JOB");
  boolean timeCheck = false, minTempCheck = false, maxTempCheck = false, onCheck = false, offCheck = false;
  int16_t data[] = {0, 0, 72, 76};  // 0 - hour, 1 - mins, 2 - minT, 3 - maxT
  RTCTime t;
  cmd_type cmd;
  uint8_t condition;
  childButton temp[room->childSize];
  String selection = "Nothing";
  if (add) {
    aj_init_once(room, temp);
    resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 0);
  } else {
    data[0] = room->job.schedules[index].cond.time.hour;
    data[1] = room->job.schedules[index].cond.time.minute;
    data[2] = room->job.schedules[index].cond.minTemp;
    data[3] = room->job.schedules[index].cond.maxTemp;
    cmd = room->job.schedules[index].command;
    selection = room->job.schedules[index].childName;
    condition = room->job.schedules[index].cond.cond_type;
    if (condition == 0) {  // 0 - timebase, 1 - tempbase, 2 - range
      timeCheck = true;
    } else if (condition == 1) {
      minTempCheck = true;
    } else if (condition == 2) {
      minTempCheck = true;
      maxTempCheck = true;
    }
    if (condition == 0 || condition == 1) {
      if (cmd == ON)  onCheck = true;
      else offCheck = true; 
    }
    dj_init_once(room, temp);
    resetDeleteJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 0);
  }
  
  
  while (1) {
    if (homeButton.isPressed()) { setIdleInterrupt(false); return HOME; }
    if (returnsButton.check(true)) { setIdleInterrupt(false); return RETURN; }
    if (nextButton.check(true)) {
      if (selection != "Nothing" && (maxTempCheck || (timeCheck || minTempCheck) && (onCheck || offCheck))) {
        if (timeCheck) { condition = 0; }
        else if (minTempCheck && !maxTempCheck) { condition = 1; }
        else if (minTempCheck && maxTempCheck) { condition = 2; }
        t.hour = data[0];
        t.minute = data[1];
        if (onCheck) { cmd = ON; }
        else { cmd = OFF; }
        uint8_t ci = getChildNameByIndex(room, selection);
        if (add) room->job.addSchedule(selection, ci, cmd, condition, t, data[2], data[3]);
        else room->job.editSchedule(index, selection, ci, cmd, condition, t, data[2], data[3]);
        setIdleInterrupt(false);
        return RETURN;
      }
    }
    if (!add && removeJobButton.check(true)) {
      debug("TODO: Finish remove job"); 
      room->job.removeSchedule(index);
      setIdleInterrupt(false);
      return RETURN;
    }
    for (int i = 0; i < room->childSize; i++) {
      if (temp[i].check(true)) {
        selection = room->childList[i].name;
        resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 1);
      }
    }
    if (timeCheckButton.check(true)) {
      if (timeCheck) {
        timeCheck = false;
      } else {
        timeCheck = true;
        minTempCheck = false;
        maxTempCheck = false;
      }
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 2);
    }
    if (minTempCheckButton.check(true)) {
      if (minTempCheck) {
        minTempCheck = false;
      } else {
        timeCheck = false;
        minTempCheck = true;
      }
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }
    if (maxTempCheckButton.check(true)) {
      if (maxTempCheck) {
        maxTempCheck = false;
      } else {
        timeCheck = false;
        minTempCheck = true;
        maxTempCheck = true;
        onCheck = false;
        offCheck = false;
      }      
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }
    if (onCheckButton.check(true)) {
      if (onCheck) {
        onCheck = false;
      } else {
        onCheck = true;
        offCheck = false;
        maxTempCheck = false;
      }      
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 4);
    }
    if (offCheckButton.check(true)) {
      if (offCheck) {
        offCheck = false;
      } else {
        onCheck = false;
        offCheck = true;
        maxTempCheck = false;
      }      
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 4);
    }
    if (hourPlusButton.check(true)) {
      if (++data[0] > 23) data[0] = 0;
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 2);
    }
    if (minPlusButton.check(true)) {
      if (++data[1] > 59) data[0] = 0;
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 2);
    }
    if (minTempPlusButton.check(true)) {
      if (++data[2] >= data[3]) ++data[3];
      if (data[3] > MAXTEMP) { --data[3]; --data[2]; }
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }
    if (maxTempPlusButton.check(true)) {
      if (++data[3] > MAXTEMP) --data[3];
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }
    if (hourMinusButton.check(true)) {
      if (--data[0] < 0) data[0] = 23;
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 2);
    }
    if (minMinusButton.check(true)) {
      if (--data[1] < 0) data[0] = 59;
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 2);
    }
    if (minTempMinusButton.check(true)) {
      if (--data[2] < MINTEMP) ++data[2];
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }
    if (maxTempMinusButton.check(true)) {
      if (--data[3] <= data[2]) --data[2];
      if (data[2] < MINTEMP) { ++data[2]; ++data[3]; }
      resetAddJobUI(room, temp, timeCheck, minTempCheck, maxTempCheck, onCheck, offCheck, selection, data, 3);
    }    
  }  
}

// section 0: ALL; 1: Str; 2: time; 3: temps; 4: command;
void resetAddJobUI(roomStruct* room, childButton* children, boolean tc, boolean mintc, boolean maxtc, boolean oc, boolean ofc, String sc, int16_t* d, uint8_t section) {
  uint16_t textColor = myScreen.rgb(77, 132, 171);
  uint8_t xoff = 1;
  if (section == 0) {
    uiBackground(true);
    nextButton.draw();
    returnsButton.draw();
    for (int i = 0; i < room->childSize; i++) {
      children[i].draw();
    }
  } 
  if (section == 1 || section == 0) {
    myScreen.drawImage(g_0120BKImage, 0, 120);
    gText(0, 120, sc + " is selected.", textColor, 2);
  }
 if (section == 2 || section == 3 || section == 0) {
    myScreen.drawImage(g_0144BKImage, 0, 144);
    gText(42, 144, "Time:", textColor, 2);
    gText(186, 144, ":", textColor, 2);
    
    if (tc) {
      timeCheckButton.dDefine(&myScreen, g_check16Image, 108, 144, setItem(201, "TC"));
    } else {
      timeCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 144, setItem(201, "TC"));
    }
    timeCheckButton.draw();
    gText(146 - xoff, 144, String(d[0]/10) + String(d[0]%10), textColor, 2);  
    gText(214 - xoff, 144, String(d[1]/10) + String(d[1]%10), textColor, 2);
  }
 if (section == 2 || section == 3 || section == 4 || section == 0) {
    myScreen.drawImage(g_0168BKImage, 0, 168);    
    gText(66, 168, String((char)0xB0) + "F:", textColor, 2);
    gText(130, 168, ">", textColor, 2);
    gText(306, 168, "<", textColor, 2);
    if (mintc) {
      minTempCheckButton.dDefine(&myScreen, g_check16Image, 108, 168, setItem(202, "MTC"));
    } else {
      minTempCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 168, setItem(202, "MTC"));
    }
    if (maxtc) {
      maxTempCheckButton.dDefine(&myScreen, g_check16Image, 216, 168, setItem(203, "MTC"));
    } else {
      maxTempCheckButton.dDefine(&myScreen, g_uncheck16Image, 216, 168, setItem(203, "MTC"));
    }
    minTempCheckButton.draw();
    maxTempCheckButton.draw();
    gText(158 - xoff, 168, String(d[2]), textColor, 2);
    gText(254 - xoff, 168, String(d[3]), textColor, 2);
  }
 if (section == 4 || section == 3 || section == 0) {
    myScreen.drawImage(g_0192BKImage, 0, 192);
    gText(6, 192, "Command:", textColor, 2);
    gText(130, 192, "ON", textColor, 2);
    gText(182, 192, "OFF", textColor, 2);
    if (oc) {
      onCheckButton.dDefine(&myScreen, g_check16Image, 108, 192, setItem(204, "OC"));
    } else {
      onCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 192, setItem(204, "OC"));
    }
    if (ofc) {
      offCheckButton.dDefine(&myScreen, g_check16Image, 160, 192, setItem(205, "OFC"));
    } else {
      offCheckButton.dDefine(&myScreen, g_uncheck16Image, 160, 192, setItem(205, "OFC"));
    }
    onCheckButton.draw();
    offCheckButton.draw();
  }
  aj_checkerEnable();
  aj_buttonDraw();
}

void resetDeleteJobUI(roomStruct* room, childButton* children, boolean tc, boolean mintc, boolean maxtc, boolean oc, boolean ofc, String sc, int16_t* d, uint8_t section) {
  resetAddJobUI(room, children, tc, mintc, maxtc, oc, ofc, sc, d, section);
  removeJobButton.draw();
}

void dj_init_once(roomStruct* room, childButton* children) {
  aj_init_once(room, children);
}

void aj_init_once(roomStruct* room, childButton* children) {
  nextButton.dDefine(&myScreen, g_nextImage, 228, 216, setItem(100, "NEXT"));
  nextButton.enable();
  removeJobButton.dDefine(&myScreen, g_deleteImage, 114, 216, setItem(100, "DELETE"));
  removeJobButton.enable();
  returnsButton.dDefine(&myScreen, g_returnSImage, 0, 216, setItem(100, "RETURNS"));
  returnsButton.enable();
  
  uint8_t xoff = (320 - room->childSize*64)/(room->childSize + 1);
  for (int i = 0; i < room->childSize; i++) {
    children[i].define(&myScreen, room->childList[i].button.getIcon(), i+xoff, 48, room->childList[i].name);
    children[i].enable();
  }
  timeCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 144, setItem(201, "TC"));
  minTempCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 168, setItem(202, "MTC"));
  maxTempCheckButton.dDefine(&myScreen, g_uncheck16Image, 216, 168, setItem(203, "MTC"));
  onCheckButton.dDefine(&myScreen, g_uncheck16Image, 108, 192, setItem(204, "OC"));
  offCheckButton.dDefine(&myScreen, g_uncheck16Image, 160, 192, setItem(205, "OFC"));
  hourPlusButton.dDefine(&myScreen, g_plusImage, 130, 144, setItem(206, "HPB"));
  minPlusButton.dDefine(&myScreen, g_plusImage, 198, 144, setItem(207, "MPB"));
  minTempPlusButton.dDefine(&myScreen, g_plusImage, 142, 168, setItem(208, "MTPB"));
  maxTempPlusButton.dDefine(&myScreen, g_plusImage, 238, 168, setItem(209, "MTPB"));
  hourMinusButton.dDefine(&myScreen, g_minusImage, 170, 144, setItem(210, "HMB"));
  minMinusButton.dDefine(&myScreen, g_minusImage, 238, 144, setItem(211, "MMB"));
  minTempMinusButton.dDefine(&myScreen, g_minusImage, 194, 168, setItem(212, "MTMB"));
  maxTempMinusButton.dDefine(&myScreen, g_minusImage, 290, 168, setItem(213, "MTMB"));
  aj_checkerEnable();
  hourPlusButton.enable();
  minPlusButton.enable();
  minTempPlusButton.enable();
  maxTempPlusButton.enable();
  hourMinusButton.enable();
  minMinusButton.enable();
  minTempMinusButton.enable();
  maxTempMinusButton.enable();
}

void aj_checkerEnable() {
  timeCheckButton.enable();
  minTempCheckButton.enable();
  maxTempCheckButton.enable();
  onCheckButton.enable();
  offCheckButton.enable();
}

void aj_buttonDraw() {
  hourPlusButton.draw();
  minPlusButton.draw();
  minTempPlusButton.draw();
  maxTempPlusButton.draw();
  hourMinusButton.draw();
  minMinusButton.draw();
  minTempMinusButton.draw();
  maxTempMinusButton.draw();
}

void resetJobConfigUI(roomStruct *room, uint8_t page, boolean multipage) {
  int x;
  jc_init_once(room);
  uiBackground(true);
  addJobButton.draw();
  if (multipage) {
    if (page == 1) {
      previousButton.enable(false);
      nextButton.enable(true);
      nextButton.draw();
      if (room->job.scheduleSize > 6) x = 6;
      else x = room->job.scheduleSize;
    } else {
      nextButton.enable(false);
      previousButton.enable(true);
      previousButton.draw();      
      x = room->job.scheduleSize;
    }
  } else {
    x = room->job.scheduleSize;
  }
  for (int i = (page-1)*6; i < x; i++) {
    room->job.schedules[i].checkBox.draw();
    room->job.schedules[i].list.draw();
  }
  returnsButton.draw();
}

void jc_init_once(roomStruct *room) {
  if (room->job.scheduleSize >= MAXSCHEDULE) {
    addJobButton.dDefine(&myScreen, g_jobAddBlackImage, 0, 48, setItem(100, "ADDJOB"));
    addJobButton.enable(false);
  } else {
    addJobButton.dDefine(&myScreen, g_jobAddImage, 0, 48, setItem(100, "ADDJOB"));
    addJobButton.enable();
  }
  previousButton.dDefine(&myScreen, g_previousImage, 0, 216, setItem(100, "PREVIOUS"));
  returnsButton.dDefine(&myScreen, g_returnSImage, 114, 216, setItem(100, "RETURNS"));
  nextButton.dDefine(&myScreen, g_nextImage, 228, 216, setItem(100, "NEXT"));
  previousButton.enable(false);
  returnsButton.enable();
  nextButton.enable(false);
}

/********************************************************************************/
/*                                Child Controls                                */
/********************************************************************************/
imageButton pairChildButton;
boolean childConfig(roomStruct* room) {
  resetChildConfigUI(room);
  while(1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetChildConfigUI(room);
    }
    if (homeButton.isPressed()) {
      return HOME;
    }
    if (returnButton.check(true)) {
       return RETURN; 
    }
    if (pairChildButton.check(true)) {
      if (pairChild(room)) return HOME;
      resetChildConfigUI(room);
    }
    for (int i = 0; i < room->childSize; i++) {
      if (room->childList[i].button.check(true)) {
        if (childControl(room, room->childList[i], i)) return HOME;
        resetChildConfigUI(room);
      }
    }
  }
}

boolean childControl(roomStruct* room, childStruct child, uint8_t index) {
  uint8_t count = 0;
  boolean celsius_c = true;
  float tmp = getChildTemp(child);
  long current = millis();
  childInfoButton.dDefine(&myScreen, g_infoCImage, xy[count][0], xy[count++][1], setItem(100, "INFO"));
  childInfoButton.enable();
  onButton.dDefine(&myScreen, g_onImage, xy[count][0], xy[count++][1], setItem(100, "ON"));
  onButton.enable();  
  offButton.dDefine(&myScreen, g_offImage, xy[count][0], xy[count++][1], setItem(100, "OFF"));
  offButton.enable();  
  removeButton.dDefine(&myScreen, g_removeImage, xy[count][0], xy[count++][1], setItem(100, "REMOVE"));
  removeButton.enable();  
  returnButton.dDefine(&myScreen, g_returnImage, xy[count][0], xy[count++][1], setItem(100, "RETURN"));
  returnButton.enable();
  resetChildControlUI(tmp, celsius_c, 0);
  
  while(1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetChildControlUI(tmp, celsius_c, 0);
    }
    if (millis() - current > 60000) {
      tmp = getChildTemp(child);
      current = millis();
      resetChildControlUI(tmp, celsius_c, 1);
    }
    if (homeButton.isPressed()) {
      return HOME;
    }
    if (returnButton.check(true)) {
      return RETURN; 
    }
    if (childInfoButton.check(true)) {
      tmp = getChildTemp(child);
      celsius_c = !celsius_c;
      resetChildControlUI(tmp, celsius_c, 1);
    }
    if (onButton.check(true)) {
      childCommand(child, "ON");
    }
    if (offButton.check(true)) {
      childCommand(child, "OFF");
    }
    if (removeButton.check(true)) {
      childCommand(child, "DEL");
      removeChild(room, index);
      return RETURN;
    }
  }
}

void resetChildControlUI(float tmp, boolean c, uint8_t type) {  // type: 0 - update all; 1 - update temp only
  uint8_t x1 = childInfoButton.getX()+28, x2 = childInfoButton.getX()+44, y1 = childInfoButton.getY()+13, y2 = childInfoButton.getY()+36;
  int16_t t = ceil(3.3 * tmp * 100.0 / 1024.0);
  uint16_t textColor = myScreen.rgb(77, 132, 171);
  if (type == 0) {
    uiBackground(true);
    onButton.draw();
    offButton.draw();
    removeButton.draw();
    returnButton.draw();
  }
  childInfoButton.draw();
  
  if (c && tmp != -1) {
    gText(x1, y1, String(t/10), textColor, 2);
    gText(x2, y1, String(t%10), textColor, 2);
    gText(x2, y2, "C", textColor, 2);
  } else if (!c && tmp != -1) {
    t = t * 9.0 / 5.0 + 32.0;
    gText(x1, y1, String(t/10), textColor, 2);
    gText(x2, y1, String(t%10), textColor, 2);
    gText(x2, y2, "F", textColor, 2);
  } else {
    gText(childInfoButton.getX()+39, childInfoButton.getY()+20, String((char)0xBF), textColor, 3);
    return;
  }
  gText(x1, y2, String((char)0xB0), textColor, 2);
}

void childCommand(childStruct child, char* cmd) {
  _listenInterrupt = true;
  while (_listening) {delay(1);}
  txPacket.node = child.node;
  txPacket.parent = ADDRESS_LOCAL;
  strcpy((char*)txPacket.msg, cmd);
  Radio.transmit(child.node, (unsigned char*)&txPacket, sizeof(txPacket));
  while(Radio.busy()){}
  if (Radio.receiverOn((unsigned char*)&rxPacket, sizeof(rxPacket), 2000) <= 0) {
    // Failed connection
    debug("No ACK from node " + String(txPacket.node));
    _listenInterrupt = false;
    return;
  }
  // Below happens when successful connected
  if (!strcmp((char*)rxPacket.msg, "ACK")) {
    debug("ACK from node " + String(rxPacket.node));
  }
  _listenInterrupt = false;
}

void removeChild(roomStruct* room, uint8_t index) {
  if (index == room->childSize-1) {
    ;
  } else {
    for (int i = 0; i < room->childSize-1; i++) {
      if (i == index) {
        index++;
        room->childList[i].name = room->childList[index].name;
        room->childList[i].type = room->childList[index].type;
        room->childList[i].button.define(&myScreen, room->childList[index].button.getIcon(), xy[i][0], xy[i][1], room->childList[i].name);
        room->childList[i].button.enable();
        room->childList[i].node = room->childList[index].node;
      }
    }
  }
  room->childSize--;
}

void resetChildConfigUI(roomStruct* room) {
  uint8_t count = 0;
  uiBackground(true);
  for (int i = 0; i < room->childSize; i++) {
    room->childList[i].button.draw();
    count++;
  }
  if (room->childSize < 5) {
    pairChildButton.dDefine(&myScreen, g_pairChildImage, xy[count][0], xy[count++][1], setItem(97, "PAIRCHILD"));
    pairChildButton.enable();
    pairChildButton.draw();
  }
  returnButton.dDefine(&myScreen, g_returnImage, xy[count][0], xy[count][1], setItem(100, "RETURN"));
  returnButton.enable();
  returnButton.draw();
}

uint8_t getChildNameByIndex(roomStruct* room, String name) {
  for (int i = 0; i <room->childSize; i++) {
    if (name ==  room->childList[i].name) return i;
  }
}

/********************************************************************************/
/*                                 Room Controls                                */
/********************************************************************************/
boolean changeRoomName(roomStruct *room) {
  String name = "";
  uint8_t key;
  initPairRoomUI();
  while (1) {
    if (homeButton.isPressed()) {
      return HOME; 
    }
    if (nextButton.check(true)) {
      if (masterName == name) {
        addErrorMessage(roomNameRepeat);
      } else if (isEmpty(name)) {
        addErrorMessage(nameEmpty);
      } else {
        room->name = name;
        KB.setEnable(false);
        setIdleInterrupt(false);
        return HOME;
      }
    }    
    key = KB.getKey();
    if (key == 0xFF) {
      delay(1);
    } else if (key == 0x08) {
      if (name.length() > 0) {
        name = name.substring(0, name.length()-1);
        updateNameField(name);
      }
    } else {
      if (name.length() < MAXNAMELENGTH) {
        name += char(key);
        updateNameField(name);
      }
    }
  }
}

void roomConfig(roomStruct *room) {
  rc_init_once();  
  resetRoomConfigUI(room);
  long current = millis();
  while(1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetRoomConfigUI(room);
    }
    if (millis() - current > 60000) {
      updateRoomInfo(room);
      current = millis();
    }
    if (roomNameButton.isPressed()) {
      roomOption(room);
      resetRoomConfigUI(room);
    }
    if (roomTempButton.isPressed()) {
      celsius = !celsius;
      updateRoomInfo(room);
    }
    if (updateButton.check(true)) {
      updateRoomInfo(room);
      current = millis();
    }
    if (optionButton.check(true)) {
      childConfig(room);
      resetRoomConfigUI(room);
    }
    if (jobButton.check(true)) {
      jobConfig(room);
      resetRoomConfigUI(room); 
    }
  }
}

boolean roomOption(roomStruct *room) {
  ro_init_once();
  resetRoomOptionUI();
  while (1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetRoomConfigUI(room);
    }
    if (homeButton.isPressed()) {
      return HOME;
    }
    if (returnButton.check(true)) {
      return RETURN;
    }
    if (roomRenameButton.check(true)) {
      if (changeRoomName(room)) return HOME;
      resetRoomConfigUI(room);
    }
    if (setTimeButton.check(true)) {
      dateSet();
      timeSet();
      resetRoomConfigUI(room);
    }
    if (removeButton.check(true)) {
      softReset();
      return HOME;
    }
  } 
}

void ro_init_once() {
  uint8_t count = 0;
  roomRenameButton.dDefine(&myScreen, g_renameImage, xy[count][0], xy[count++][1], setItem(101, "RENAME"));
  roomRenameButton.enable();
  setTimeButton.dDefine(&myScreen, g_setTimeImage, xy[count][0], xy[count++][1], setItem(101, "SETTIME"));
  setTimeButton.enable();
  removeButton.dDefine(&myScreen, g_removeImage, xy[count][0], xy[count++][1], setItem(101, "REMOVE"));
  removeButton.enable();
  returnButton.dDefine(&myScreen, g_returnImage, xy[count][0], xy[count++][1], setItem(101, "RETURN"));
  returnButton.enable();
}

void resetRoomOptionUI() {
  uiBackground(true);
  roomRenameButton.draw();
  setTimeButton.draw();
  removeButton.draw();
  returnButton.draw();
}

void rc_init_once() {
  uint8_t count = 3; 
  updateButton.dDefine(&myScreen, g_updateImage, xy[count][0], xy[count][1], setItem(100, "UPDATE"));
  updateButton.enable();
  optionButton.dDefine(&myScreen, g_optionImage, xy[count+1][0], xy[count+1][1], setItem(100, "OPTION"));
  optionButton.enable();
  jobButton.dDefine(&myScreen, g_jobImage, xy[count+2][0], xy[count+2][1], setItem(100, "JOB"));
  jobButton.enable();
  roomNameButton.dDefine(&myScreen, 35, 60, 134, 64, setItem(0, "NAME"), whiteColour, redColour);
  roomNameButton.enable();
  roomTempButton.dDefine(&myScreen, 125, 60, 102, 64, setItem(0, "TEMP"), whiteColour, redColour);
  roomTempButton.enable();
}

void resetRoomConfigUI(roomStruct* room) {
  uiBackground(false);
  updateRoomInfo(room);  
  updateButton.draw();  
  optionButton.draw();
  jobButton.draw();
}

void updateRoomInfo(roomStruct* room) {
  uint8_t x = 28, y = 60;
  myScreen.drawImage(g_infoImage, 28, 60);
  gText(x + 45, y + 20, room->name, whiteColour, 3);
  room->roomTempC = (int16_t)ceil(getAverageTemp(room));
  room->roomTempF = room->roomTempC  * 9 / 5 + 32;
  if (celsius) {
    gText(x + 177,y + 20, String(room->roomTempC) + (char)0xB0 + "C", whiteColour, 3);
  } else {
    gText(x + 177,y + 20, String(room->roomTempF) + (char)0xB0 + "F", whiteColour, 3);
  }
}

float getAverageTemp(roomStruct* room) {
  float tempF = 0.0;
  uint8_t count = 0;
  if (room->childSize > 0) {
    tempF += getChildrenTemp(room);
    count = 1;
  }
  tempF = (tempF + getLocalTemp()) / (float)++count;
  return tempF;
}

float getChildrenTemp(roomStruct* room) {
  _listenInterrupt = true;
  while (_listening) {delay(1);}
  uint16_t temp = 0, count = 0;
  for (int i = 0; i < room->childSize; i++) {
    float t2 = getChildTemp(room->childList[i]);
    debug(String(t2));
    if (t2 > 0) {
      temp += t2;
      count++;
    }
  }
  _listenInterrupt = false;
  return VREF * ((float)temp / count) * 100.0 / 1024.0;
}

float getChildTemp(childStruct child) {
  strcpy((char*)txPacket.msg, "TEMP");
  uint8_t node = child.node;
  txPacket.parent = ADDRESS_LOCAL;
  txPacket.node = node;
  Radio.transmit(node, (unsigned char*)&txPacket, sizeof(txPacket));
  while(Radio.busy()){}
  if (Radio.receiverOn((unsigned char*)&rxPacket, sizeof(rxPacket), 2000) <= 0) {
    // Failed connection
    return -1.0;
  }
  // Below happens when successful connected
  if (!strcmp((char*)rxPacket.msg, "ACK")) {
    debug("ACK from node " + String(rxPacket.node));    
    return ((rxPacket.upper << 8) | rxPacket.lower);
  }
  return -1.0;
}

float getLocalTemp() {
  int val = 0;
  for (int i = 0; i < 10; i++) {
    val = analogRead(SENSOR);
  }
  val += analogRead(SENSOR);
  val += analogRead(SENSOR);
  val += analogRead(SENSOR);
  return VREF * ((float)val / 4.0) * 100.0 / 4096.0;
}

/********************************************************************************/
/*                                   New Child                                  */
/********************************************************************************/
boolean pairChild(roomStruct *room) {
  imageButton pairFan, pairVent, pairBlind;
  pairFan.dDefine(&myScreen, g_pairFanImage, xy[0][0], xy[0][1], setItem(89, "PAIRFAN"));
  pairFan.enable();  
  pairVent.dDefine(&myScreen, g_pairVentImage, xy[1][0], xy[1][1], setItem(88, "PAIRVENT"));
  pairVent.enable();  
  pairBlind.dDefine(&myScreen, g_pairBlindImage, xy[2][0], xy[2][1], setItem(87, "PAIRBLIND"));
  pairBlind.enable();  
  returnButton.dDefine(&myScreen, g_returnImage, xy[3][0], xy[3][1], setItem(100, "RETURN"));
  returnButton.enable();
  resetPairChild(pairFan, pairVent, pairBlind);
  
  while(1) {
    if (_idle) {
      while(_idle){delay(1);}
      resetPairChild(pairFan, pairVent, pairBlind);
    }   
    if (homeButton.isPressed()) {
      debug("Home is pressed");
      return HOME;
    }
    if (returnButton.check(true)) {
      debug("Return is pressed");
      return RETURN;
    }
    if (pairFan.check(true)) {
      if (newChild(room, NEWFAN)) return HOME;
    }
    if (pairVent.check(true)) {
      if (newChild(room, NEWVENT)) return HOME;
    }
    if (pairBlind.check(true)) {
      if (newChild(room, NEWBLIND)) return HOME;
    }
  }
}

void resetPairChild(imageButton pairFan, imageButton pairVent, imageButton pairBlind) {
  uiBackground(true);
  pairFan.draw();
  pairVent.draw();
  pairBlind.draw();
  returnButton.draw();
}

boolean newChild(roomStruct *room, uint8_t type) {
  setIdleInterrupt(true);
  String name = "";
  uint8_t key;
  boolean flag;  
  uiKeyboardArea();
  if (type == NEWFAN) {
    newPairMessage("FAN");
  } else if (type == NEWVENT) {
    newPairMessage("VENT");
  } else if (type == NEWBLIND) {
    newPairMessage("BLIND");
  } else {
    ;
  }
  while (1) {
    if (homeButton.isPressed()) {
      setIdleInterrupt(false);
      return HOME; 
    }
    if (nextButton.check(true)) {
      if (!isEmpty(name)) {
        if (room->childSize < MAXCHILDSIZE) {
          if (room->childSize > 0 && repeatChildName(room, name)) {
            addErrorMessage(childNameRepeat);
          } else {
            if (type == NEWFAN) {
              if (room->f >= 11) {
                childExceeded(FAN);
                setIdleInterrupt(false);
                return HOME;
              }              
              if (flag = addChild(room, name, FAN, g_fanImage)) debug("FAN added");
              else debug("Failed connect FAN");
            } else if (type == NEWVENT) {
              if (room->v >= 11) {
                childExceeded(VENT);
                setIdleInterrupt(false);
                return HOME;
              }
              if (flag = addChild(room, name, VENT, g_ventImage)) debug("VENT added");
              else debug("Failed connect VENT");
            } else if (type == NEWBLIND) {
              if (room->b >= 11) {
                childExceeded(BLIND);
                setIdleInterrupt(false);
                return HOME;
              }
              if (flag = addChild(room, name, BLIND, g_blindImage)) debug("BLIND added");
              else debug("Failed connect BLIND");
            } else {
              KB.setEnable(false);
              setIdleInterrupt(false);
              return HOME;
            }
            if (flag) {
              KB.setEnable(false);
              setIdleInterrupt(false);
              return HOME;
            }
          }          
        } else {
          maxNumberError("Children");
          KB.setEnable(false);
          delay(3000);
          setIdleInterrupt(false);
          return HOME;
        }        
      } else {
        addErrorMessage(nameEmpty);
      }
    }
    
    key = KB.getKey();
    if (key == 0xFF) {
      delay (1); 
    } else if (key == 0x0D) {
      KB.setEnable(false);
      setIdleInterrupt(false);
      return HOME;
    } else if (key == 0x08) {
      if (name.length() > 0) {
        name = name.substring(0, name.length()-1);
        updateNameField(name);
      } 
    } else {
      if (name.length() < MAXNAMELENGTH) {
        name += char(key);
        updateNameField(name);
      } 
    }
  }
}

boolean addChild(roomStruct *room, String name, child_t type, const uint8_t *icon) {
  uint8_t childSize = room->childSize;
  uint8_t position = childSize % 6;
  /******* Connecting ******/
  _listenInterrupt = true;
  while (_listening) {delay(1);}
  addErrorMessage(connecting);
  uint8_t newNode = getChildNode(room, type) + 1;
  txPacket.node = newNode;
  strcpy((char*)txPacket.msg, "PAIR");
  Radio.transmit((uint8_t)type, (unsigned char*)&txPacket, sizeof(txPacket));
  while(Radio.busy()){}
  if (Radio.receiverOn((unsigned char*)&rxPacket, sizeof(rxPacket), 30000) <= 0) {
    // Failed connection
    addErrorMessage(timeout);
    _listenInterrupt = false;
    return false;
  }
  // Below happens when successful connected
  if (!strcmp((char*)rxPacket.msg, "ACK")) {
    debug(String(rxPacket.node) + " " + String((char*)rxPacket.msg));
    room->childList[childSize].name = name;
    room->childList[childSize].type = type;
    room->childList[childSize].button.define(&myScreen, icon, xy[position][0], xy[position][1], name);
    room->childList[childSize].button.enable();
    room->childList[childSize].node = newNode;  
    room->childSize++;
    increment(room, type);
    _listenInterrupt = false;
    return true;
  }
  addErrorMessage(retry);
  _listenInterrupt = false;
  return false;
}

uint8_t getChildNode(roomStruct *room, child_t type) {
  switch(type) {
  case VENT:
    return VENT + ADDRESS_LOCAL + room->v;
    break;
  case FAN:
    return FAN + ADDRESS_LOCAL + room->f;
    break;
  case BLIND:
    return BLIND + ADDRESS_LOCAL + room->b;
    break;
  }
}

void childExceeded (child_t type) {
  addErrorMessage(getChildTypeString(type) + outOfLimit);
  KB.setEnable(false);
  delay(2000);
}

void increment(roomStruct *room, child_t type) {
  switch(type) {
  case VENT:
    room->v++;
    break;
  case FAN:
    room->f++;
    break;
  case BLIND:
    room->b++;
    break;
  }
}

String getChildTypeString(child_t type) {
  switch (type) {
    case VENT:
      return "vent";
      break;
    case FAN:
      return "fan";
      break;
    case BLIND:
      return "blind";
      break;
    default:
      return "";
      break;
  } 
}

/********************************************************************************/
/*                                   New Rooms                                  */
/********************************************************************************/
void initPairRoomUI() {
  setIdleInterrupt(true);
  uiKeyboardArea();
  gText(10, 44, "Name this room...", whiteColour, 3);
  gText(10, 70, "Name: ", whiteColour, 3);
  gText(210, 82, "Max " + String(MAXNAMELENGTH) + " letters", whiteColour, 1);
}

boolean pairRoom() {
  String name = "";
  uint8_t key;
  initPairRoomUI();
  while (1) {
    if (nextButton.check(true)) {
      if (!isEmpty(name)) {
        newRoom(name);
        KB.setEnable(false);
        setIdleInterrupt(false);
        return HOME;
      } else {
        addErrorMessage(nameEmpty);
      }
    }    
    key = KB.getKey();
    if (key == 0xFF) {
      delay(1);
    } else if (key == 0x08) {
      if (name.length() > 0) {
        name = name.substring(0, name.length()-1);
        updateNameField(name);
      }
    } else {
      if (name.length() < MAXNAMELENGTH) {
        name += char(key);
        updateNameField(name);
      }
    }
  }
}

void newRoom(String name) {
  thisRoom.name = name;
  thisRoom.node = ADDRESS_LOCAL;
  thisRoom.type = TYPE;
  thisRoom.childSize = 0;
}

/********************************************************************************/
/*                                  UI Related                                  */
/********************************************************************************/
void uiBackground(boolean flag) {
  myScreen.clear(blackColour);
  myScreen.drawImage(g_bgImage, 0, 0);
  if (flag) {
    homeButton.dDefine(&myScreen, 10, 4, 32, 32, setItem(0, "HOME"), whiteColour, redColour);
    homeButton.enable();
  }
}

void uiKeyboardArea() {
  nextButton.dDefine(&myScreen, g_nextImage, 223, 96, setItem(80, "NEXT"));
  nextButton.enable();
  uiBackground(true);
  updateNameField("");
  nextButton.draw();
  KB.draw();
}
/*****************************************************************************************/
/*                                  Time & Date Related                                  */
/*****************************************************************************************/
void updateTime() {
  if (!timeInterrupt) {
    RTCTime time;
    if (updateTimeMode == TIMEMODE) {
      if (!timeInterrupt) {
        _updateTime(&time);
        String wday = String(wd[(int)time.wday][0]) + String(wd[(int)time.wday][1]) + String(wd[(int)time.wday][2]);
        String _t = String(time.hour) + ":" + time.minute + ":" + time.second;
        uint16_t color = whiteColour;
        if (time.wday == 0 || time.wday == 6) color = redColour;
        gText(142, 8, wday, color, 3);
        gText(320 - _t.length() * 16, 8, _t, whiteColour, 3);
        delay(1000);
      }
    } else {
      if (!timeInterrupt) {
        _updateTime(&time);
        String date = String(time.month) + "/" + time.day + "/" + time.year;
        gText(320 - date.length() * 16, 8, date, whiteColour, 3);
        delay(1000);
      }
    }
  } else {
    delay(1); 
  }
}

void _updateTime(RTCTime *time) {
  RTC.GetAll(time);
  myScreen.drawImage(g_timebarImage, 80, 0);
  myScreen.setFontSize(3);
}

void dateSet() {
  setTimeInterrupt(true);
  setIdleInterrupt(true);
  button timeSetOK;
  imageButton monthUp, dayUp, yearUp, monthDown, dayDown, yearDown;
  
  /* init elements */
  timeSetOK.dDefine(&myScreen, 228, 195, 60, 40, setItem(0, "OK"), blueColour, yellowColour);
  monthUp.dDefine(&myScreen, g_upImage, 64, 87, setItem(0, "UP"));
  dayUp.dDefine(&myScreen, g_upImage, 128, 87, setItem(0, "UP"));
  yearUp.dDefine(&myScreen, g_upImage, 200, 87, setItem(0, "UP"));
  monthDown.dDefine(&myScreen, g_downImage, 64, 153, setItem(0, "DOWN"));
  dayDown.dDefine(&myScreen, g_downImage, 128, 153, setItem(0, "DOWN"));
  yearDown.dDefine(&myScreen, g_downImage, 200, 153, setItem(0, "DOWN"));
  
  timeSetOK.enable();
  monthUp.enable();
  dayUp.enable();
  yearUp.enable();
  monthDown.enable();
  dayDown.enable();
  yearDown.enable();
  /*****************/
  
  uint8_t mon = 1, day = 1;
  uint16_t year = 1970;
  char str[13] = "0 1/0 1/1970";
  if (firstInitialized) {
    RTCTime time;
    RTC.GetAll(&time);
    mon = time.month;
    day = time.day;
    year = time.year;
    str[0] = mon / 10 + '0';    str[2] = mon % 10 + '0';
    str[4] = day / 10 + '0';    str[6] = day % 10 + '0';
    str[8] = year / 1000 + '0';    str[9] = year % 1000 / 100 + '0';    str[10] = year % 100 / 10  + '0';    str[11] = year % 10  + '0';
  }
  
  /* draw element */  
  ds_ui_init(str, monthUp, dayUp, yearUp, monthDown, dayDown, yearDown, timeSetOK);
  
  while(1) {
    if (timeSetOK.isPressed()) {
      debug("Date set " + String(str));
      RTC.SetDate(day, mon, year%100, (uint8_t)(year / 100));
      setTimeInterrupt(false);
      setIdleInterrupt(false);
      return;
    }
    myScreen.setPenSolid(true);
    if (monthUp.check(true)) {
      if (++mon > 12) mon = 1;
      str[0] = mon/10 + '0';
      str[2] = mon%10 + '0';
      drawTimeDateSetting(64, str);
    }
    if (dayUp.check(true)) {
      if (mon == 1 || mon == 3 || mon == 5 || mon == 7 || mon == 8 || mon == 10 || mon == 12) {
        if (++day > 31) day = 1; 
      } else if (mon == 2) {
        if (year%4 == 0) {
          if (++day > 29) day = 1;    
        } else {
          if (++day > 28) day = 1;   
        }
      } else {
        if (++day > 30) day = 1; 
      }
      str[4] = day/10 + '0';
      str[6] = day%10 + '0';
      drawTimeDateSetting(64, str);
    }
    if (yearUp.check(true)) {
      if (++year > 2100) year = 2100;
      str[8] = year / 1000 + '0';
      str[9] = year % 1000 / 100 + '0';
      str[10] = year % 100 / 10  + '0';
      str[11] = year % 10  + '0';
      drawTimeDateSetting(64, str);
    }
    if (monthDown.check(true)) {
      if (--mon < 1) mon = 12;
      str[0] = mon/10 + '0';
      str[2] = mon%10 + '0';
      drawTimeDateSetting(64, str);
    }
    if (dayDown.check(true)) {
      if (mon == 1 || mon == 3 || mon == 5 || mon == 7 || mon == 8 || mon == 10 || mon == 12) {
        if (--day < 1) day = 31; 
      } else if (mon == 2) {
        if (year%4 == 0) {
          if (--day < 1) day = 29;    
        } else {
          if (--day < 1) day = 28;   
        }
      } else {
        if (--day < 1) day = 30; 
      }
      str[4] = day/10 + '0';
      str[6] = day%10 + '0';
      drawTimeDateSetting(64, str);
    }
    if (yearDown.check(true)) {
      if (--year < 1970) year = 1970;
      str[8] = year / 1000 + '0';
      str[9] = year % 1000 / 100 + '0';
      str[10] = year % 100 / 10  + '0';
      str[11] = year % 10  + '0';
      drawTimeDateSetting(64, str);
    }
  }
}

void ds_ui_init(char* str, imageButton mu, imageButton du, imageButton yu, imageButton md, imageButton dd, imageButton yd, button ts) {
  myScreen.clear(whiteColour);
  dsts_ui_init(56, "Today's date?");
  mu.draw();  du.draw();  yu.draw();
  drawTimeDateSetting(64, str);
  md.draw();  dd.draw();  yd.draw();
  ts.draw(true);
}

void timeSet() {
  setTimeInterrupt(true);
  setIdleInterrupt(true);
  button timeSetOK;
  imageButton dayUp, hourUp, minUp, secUp, dayDown, hourDown, minDown, secDown;
  item timeSetOK_i, up_i, down_i;
  
  /* init elements */
  timeSetOK_i = setItem(0, "OK");
  up_i = setItem(0, "UP");
  down_i = setItem(0, "DOWN");
  
  timeSetOK.dDefine(&myScreen, 228, 195, 60, 40, timeSetOK_i, blueColour, yellowColour);
  dayUp.dDefine(&myScreen, g_upImage, 40, 87, up_i);
  hourUp.dDefine(&myScreen, g_upImage, 104, 87, up_i);
  minUp.dDefine(&myScreen, g_upImage, 168, 87, up_i);
  secUp.dDefine(&myScreen, g_upImage, 232, 87, up_i);
  dayDown.dDefine(&myScreen, g_downImage, 40, 153, down_i);
  hourDown.dDefine(&myScreen, g_downImage, 104, 153, down_i);
  minDown.dDefine(&myScreen, g_downImage, 168, 153, down_i);
  secDown.dDefine(&myScreen, g_downImage, 232, 153, down_i);
  
  timeSetOK.enable();
  dayUp.enable();
  hourUp.enable();
  minUp.enable();
  secUp.enable();
  dayDown.enable();
  hourDown.enable();
  minDown.enable();
  secDown.enable();
  /*****************/
  
  int8_t hour = 0, mins = 0, secs = 0, day = 1;
  char str[16] = "MON 0 0:0 0:0 0";
  if (firstInitialized) {
    RTCTime time;
    RTC.GetAll(&time);
    hour = time.hour;
    mins = time.minute;
    secs = time.second;
    day = (uint8_t)time.wday;
    str[0] = wd[day][0];    str[1] = wd[day][1];    str[2] = wd[day][2];
    str[4] = hour / 10 + '0';    str[6] = hour % 10 + '0';
    str[8] = mins / 10 + '0';    str[10] = mins % 10 + '0';
    str[12] = secs / 10 + '0';    str[14] = secs % 10 + '0';
  }
  debug(String(str));
  
  /* draw element */
  ts_ui_init(str, dayUp, hourUp, minUp, secUp, dayDown, hourDown, minDown, secDown, timeSetOK);
  
  while(1) {
    if (timeSetOK.isPressed()) {
      debug("Time set " + String(str));
      RTC.SetTime(hour, mins, secs, day);
      setTimeInterrupt(false);
      setIdleInterrupt(false);
      return;
    }
    myScreen.setPenSolid(true);
    if (dayUp.check(true)) {
      if (++day > 6) day = 0;
      str[0] = wd[day][0];
      str[1] = wd[day][1];
      str[2] = wd[day][2];
      drawTimeDateSetting(40, str);
    }
    if (hourUp.check(true)) {
      if (++hour > 23) hour = 0;
      str[4] = hour / 10 + '0';
      str[6] = hour % 10 + '0';
      drawTimeDateSetting(40, str);
    }
    if (minUp.check(true)) {
      if (++mins > 59) mins = 0;
      str[8] = mins / 10 + '0';
      str[10] = mins % 10 + '0';
      drawTimeDateSetting(40, str);
    }
    if (secUp.check(true)) {
      if (++secs > 59) secs = 0;
      str[12] = secs / 10 + '0';
      str[14] = secs % 10 + '0';
      drawTimeDateSetting(40, str);
    }
    if (dayDown.check(true)) {
      if (--day < 0) day = 6;
      str[0] = wd[day][0];
      str[1] = wd[day][1];
      str[2] = wd[day][2];
      drawTimeDateSetting(40, str);
    }
    if (hourDown.check(true)) {
      if (--hour < 0) hour = 23;
      str[4] = hour / 10 + '0';
      str[6] = hour % 10 + '0';
      drawTimeDateSetting(40, str);
    }
    if (minDown.check(true)) {
      if (--mins < 0) mins = 59;
      str[8] = mins / 10 + '0';
      str[10] = mins % 10 + '0';
      drawTimeDateSetting(40, str);
    }
    if (secDown.check(true)) {
      if (--secs < 0) secs = 59;
      str[12] = secs / 10 + '0';
      str[14] = secs % 10 + '0';
      drawTimeDateSetting(40, str);
    }
  }  
}

void ts_ui_init(char* str, imageButton du, imageButton hu, imageButton mu, imageButton su, imageButton dd, imageButton hd, imageButton md, imageButton sd, button ts) {
  myScreen.clear(whiteColour);
  dsts_ui_init(8, "How about the time?");
  du.draw();  hu.draw();  mu.draw();  su.draw();
  dd.draw();  hd.draw();  md.draw();  sd.draw();
  drawTimeDateSetting(40, str);
  ts.draw(true);
}

void dsts_ui_init(uint8_t x, String text) {
  myScreen.clear(whiteColour);
  gText(x, 51, text, grayColour, 3);
  gText(x-1, 50, text, blueColour, 3);
}

void drawTimeDateSetting(uint8_t x, char* str) {
  myScreen.rectangle(0, 124, 319, 148, whiteColour);
  gText(x, 124, str, blackColour, 3);
}

/************************************************************************************/
/*                                  Error Messages                                  */
/************************************************************************************/
void newPairMessage(String s) {
  gText(10, 44, "Pairing new " + s + "...", whiteColour, 3);
  gText(10, 70, "Name: ", whiteColour, 3);
  gText(210, 82, "Max " + String(MAXNAMELENGTH) + " letters", whiteColour, 1);
}

void addErrorMessage(String s) {
  myScreen.drawImage(g_10102BKImage, 10, 102);
  gText(10, 102, s, redColour, 1);
}

void maxNumberError(String s) {
  uiBackground(true);
  gText(8, 60, "Cannot add " + s + "...", redColour, 3);
  gText((320 - ((13+s.length()) * 12)) >> 1, 90, "Max " + s + " achieved", redColour, 2);
  delay(3000);
}

/**********************************************************************************/
/*                                  Initializing                                  */
/**********************************************************************************/
boolean firstInit() {
  RTC.begin();
  KB.begin(&myScreen);
  dateSet();
  deviceTimeInit();
  thisRoom.job.init(&myScreen);
  Scheduler.startLoop(updateTime);
  Scheduler.startLoop(timeModeToggle);
  return pairRoom();
}

void initAIR() {
  // init AIR module
  Radio.begin(ADDRESS_LOCAL, CHANNEL_1, POWER_MAX);
  memset(rxPacket.msg, 0, sizeof(rxPacket.msg));
  memset(txPacket.msg, 0, sizeof(txPacket.msg));
  txPacket.parent = ADDRESS_LOCAL;
}

void initLCD() {
  myScreen.begin();
  myScreen.setOrientation(1);
  myScreen.setFontSize(myScreen.fontMax());
  myScreen.clear(blackColour);
  opening();
  myScreen.calibrateTouch();
}

void deviceTimeInit() {
  timeSet();
  updateTimeMode = TIMEMODE;
  updateTimeModeToggle.dDefine(&myScreen, 140, 1, 180, 39, setItem(233, "TOGGLE"), whiteColour, blackColour);
  updateTimeModeToggle.enable();
}

void opening() {
  // print Logo for 5 second
  myScreen.drawImage(g_logoImage, 0, 0);
  delay(5000);
  myScreen.clearScreen();
}

/***************************************************************************/
/*                                  Utils                                  */
/***************************************************************************/
void updateCurrentRoomTemp() {
  thisRoom.roomTempC = (int16_t)ceil(getAverageTemp(&thisRoom));
  thisRoom.roomTempF = thisRoom.roomTempC  * 9 / 5 + 32;
  debug("Room temperature auto updated: " + String(thisRoom.roomTempC) + "C " + thisRoom.roomTempF + "F");
  delay(30000);
}

void updateNameField(String s) {
  myScreen.drawImage(g_9624WhiteImage, 106, 70);
  gText(106, 70, s, whiteColour, 3);
}

boolean isEmpty(String s) {
  if (s.length() == 0) return true;
  if (s.charAt(s.length() - 1) == ' ') {
    return true && isEmpty(s.substring(0, s.length() - 1));
  } else {
    return false && isEmpty(s.substring(0, s.length() - 1));
  }
}

boolean repeatChildName(roomStruct *room, String name) {
  for (int i = 0; i < room->childSize; i++) {
    if (name == room->childList[i].name) return true;
  }
  return false;
}

void debug(String s) {
  if (DEBUG)
    Serial.println(s);
}

void idle() {
  if (!_idle && !_idleInterrupt && ++_timer == 5000) {
    debug("Screen enters idle");
    _idle = true;
    drawIdle();    
    setTimeInterrupt(true); 
//    myScreen.setBacklight(true);
  }
  if (myScreen.isTouch()) {
    _timer = 0;
    if (_idle) {
      debug("Screen leaves idle");
      _idle = false;
      setTimeInterrupt(false);
//      myScreen.setBacklight(false);
    }
  }  
}

void drawIdle() {
  myScreen.drawImage(g_idleImage, 0, 0);
}

void gText(uint16_t x, uint16_t y, String s, uint16_t c, uint8_t size) {
  myScreen.setFontSize(size);
  myScreen.gText(x, y, s, true, c); 
}

void timeModeToggle() {
  delay(5000);
  updateTimeMode = 2;
  delay(5000);
  updateTimeMode = 1;
}

void setTimeInterrupt(boolean flag) {
  timeInterrupt = flag;
}

void setIdleInterrupt(boolean flag) {
  _idleInterrupt = flag;
}

void softReset() {
  if (connectedToMaster) {
    connectedToMaster = false;
    digitalWrite(STATUS, LOW);  // turn off indicator
  }
}
