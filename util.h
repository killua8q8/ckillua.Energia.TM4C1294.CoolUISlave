#include <Energia.h>
#include "Job.h"

#define HOME true
#define RETURN false
#define SENSOR PE_1
#define NEWFAN 0x01
#define NEWVENT 0x02
#define NEWBLIND 0x03
#define MAXCHILDSIZE 5
#define MAXNAMELENGTH 6
#define MAXTEMP 255
#define MINTEMP 36
#define TIMEMODE 1
#define DATEMODE 2
#define VREF 3.3
#define STATUS PN_1  //LED1 indicating connection to MASTER

extern const uint8_t g_logoImage[];
extern const uint8_t g_room[];
extern const uint8_t g_downImage[];
extern const uint8_t g_upImage[];
extern const uint8_t g_bgImage[];
extern const uint8_t g_optionImage[];
extern const uint8_t g_timebarImage[];
//extern const uint8_t g_addImage[];
extern const uint8_t g_setTimeImage[];
extern const uint8_t g_pairSlaveImage[];
extern const uint8_t g_pairChildImage[];
extern const uint8_t g_pairFanImage[];
extern const uint8_t g_pairVentImage[];
extern const uint8_t g_pairBlindImage[];
extern const uint8_t g_returnImage[];
extern const uint8_t g_nextImage[];
extern const uint8_t g_9624WhiteImage[];
extern const uint8_t g_fanImage[];
extern const uint8_t g_blindImage[];
extern const uint8_t g_ventImage[];
extern const uint8_t g_infoImage[];
extern const uint8_t g_infoCImage[];
extern const uint8_t g_updateImage[];
extern const uint8_t g_removeImage[];
extern const uint8_t g_onImage[];
extern const uint8_t g_offImage[];
extern const uint8_t g_jobImage[];
extern const uint8_t g_jobAddImage[];
extern const uint8_t g_jobAddBlackImage[];
extern const uint8_t g_deleteImage[];
extern const uint8_t g_previousImage[];
extern const uint8_t g_returnSImage[];
extern const uint8_t g_check16Image[];
extern const uint8_t g_minusImage[];
extern const uint8_t g_plusImage[];
extern const uint8_t g_uncheck16Image[];
extern const uint8_t g_0120BKImage[];
extern const uint8_t g_0144BKImage[];
extern const uint8_t g_0168BKImage[];
extern const uint8_t g_0192BKImage[];
extern const uint8_t g_10102BKImage[];
extern const uint8_t g_idleImage[];
extern const uint8_t g_acFanImage[];
extern const uint8_t g_autoImage[];
extern const uint8_t g_coolImage[];
extern const uint8_t g_heatImage[];
extern const uint8_t g_hvacImage[];
extern const uint8_t g_roomListImage[];
extern const uint8_t g_renameImage[];

const String nameEmpty = "Name cannot be empty";
const String roomNameRepeat = "Room name existed";
const String childNameRepeat = "Child name existed";
const String timeout = "Timeout, please retry";
const String retry = "Error, please retry";
const String outOfLimit = " reaches max limit";
const String connecting = "Connecting...";

typedef enum {
  VENT = 0x60,
  FAN = 0x70,
  BLIND = 0x80
} child_t;

typedef enum {
  MASTER, SLAVE
} control_t;

// RF packet struct
struct sPacket
{
  uint8_t upper, lower;
  uint8_t parent;
  uint8_t node;
  uint8_t msg[56];
};
// RF packet struct for master communication
struct mPacket {
  uint8_t name[10];
  uint8_t node, master;
  uint8_t data;
  uint8_t tempF, tempC;
  uint8_t msg[45];
};
struct cPacket {
  uint8_t name[MAXCHILDSIZE][8];
  uint8_t node[MAXCHILDSIZE];
  uint8_t type[MAXCHILDSIZE];
  uint8_t msg[10];
};
struct jPacket {
  uint8_t childIndex[12];
  uint8_t cond[12];
  uint8_t cmd[12];
  uint8_t data1[12];
  uint8_t data2[12];
};
struct kPacket {
  uint8_t childSize;
  uint8_t enable[12];
  uint8_t msg[47]; 
};

typedef struct childStruct {
  String name;
  uint8_t node;
  child_t type;  
  childButton button;
} childStruct;

typedef struct roomStruct {
  String name;
  uint8_t node;
  control_t type;
  uint8_t childSize;
  uint8_t v = 0,f = 0,b = 0;
  int16_t roomTempC, roomTempF;
  childStruct childList[MAXCHILDSIZE];
  Job job;
} roomStruct;

int xy[6][2] = {
  {28, 60}, {128, 60}, {228, 60}, {28, 156}, {128, 156}, {228, 156}
};


