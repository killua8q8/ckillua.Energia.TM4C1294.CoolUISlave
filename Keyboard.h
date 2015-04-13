#include <Energia.h>
#include <Screen_K35.h>
#include <LCD_graphics.h>
#include <LCD_GUI.h>

#define BUFFER 255

extern const uint8_t g_keyboardImage[];

const char keyOrder[4][11] = {
  {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0x08},
  {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '\''},
  {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '(', ')'},
  {'Z', 'X', 'C', 'V', 'B', 'N', 'M', '.', '_', '-', ' '}
};

class Keyboard {
  
  public:
    Keyboard();
    void begin(Screen_K35* k35);
    void draw();
    uint8_t getKey();
    void setEnable(boolean flag = true);
 
  private:  
    void _define();
    static void _getKey();
    static void row1();
    static void row2();
    static void row3();
    static void row4();
    void writeBuffer(uint8_t c);

  protected: 
    int _r;
    int _c;
    int _w;
    int _h;
    int bufferIndex, bufferSize;
    boolean enable, _onWriteBuffer, init;
    Screen_K35* _k35;
    area keys[4][11];
    area rows[4];
    uint8_t buffer[BUFFER];
};

extern Keyboard KB;
