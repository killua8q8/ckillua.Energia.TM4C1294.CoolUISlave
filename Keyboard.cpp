#include "Keyboard.h"
#include <string.h>

Keyboard::Keyboard() {
  _r = 4;
  _c = 11;
  _w = 29;
  _h = 29;
  bufferIndex = 0;
  bufferSize = 0;
  enable = false;
  _onWriteBuffer = false;
  init = false;
}

void Keyboard::begin(Screen_K35* k35) {
  if (!init) {
    _k35 = k35;  
    _define();
    init = true;
  }
}

void Keyboard::setEnable(boolean flag) {
  KB.enable = flag;
}

void Keyboard::draw() {
  _k35->drawImage(g_keyboardImage, 0, 123);
  setEnable();
};

uint8_t Keyboard::getKey() {
//  Serial.println("Buffer size: " + String(bufferSize));
  _getKey();
  if (bufferSize > 0 && !_onWriteBuffer) {
    _onWriteBuffer = true;
    uint8_t c = buffer[0];
    Serial.println("c : " + String(c));
    memmove((char*)&buffer[0], (char*)&buffer[1], strlen((char*)buffer));
    bufferSize--;   
    _onWriteBuffer = false; 
    return c;
  } else {
    return 0xff; 
  }
};

void Keyboard::_define() {
  for (int i = 0; i < _r; i++) {
    for (int j = 0; j < _c; j++) {
      keys[i][j].dDefine(_k35, 0 + j*_w , 123 + i*_h, _w, _h, 0);
      keys[i][j].enable();
    }
    rows[i].dDefine(_k35, 0, 123 + i*_h, 320, _h, 0);
    rows[i].enable();
  }
}

void Keyboard::_getKey() {
  if (KB.rows[0].check(true)) {
//    Serial.println("row 1");
    row1();
  }
  if (KB.rows[1].check(true)) {
//    Serial.println("row 2");
    row2();
  }
  if (KB.rows[2].check(true)) {
//    Serial.println("row 3");
    row3();
  }
  if (KB.rows[3].check(true)) {
//    Serial.println("row 4");
    row4();
  }
}

void Keyboard::row1() {
  for (int i = 0; i < 6; i++) {
    if (KB.keys[0][5+i].check(true)) {
      KB.writeBuffer(keyOrder[0][5+i]);
    } else if (KB.keys[0][5-i].check(true)) {
      KB.writeBuffer(keyOrder[0][5-i]);
    }
  }
}

void Keyboard::row2() {
  for (int i = 0; i < 6; i++) {
    if (KB.keys[1][5+i].check(true)) {
      KB.writeBuffer(keyOrder[1][5+i]);
    } else if (KB.keys[1][5-i].check(true)) {
      KB.writeBuffer(keyOrder[1][5-i]);
    }
  }
}

void Keyboard::row3() {
  for (int i = 0; i < 6; i++) {
    if (KB.keys[2][5+i].check(true)) {
      KB.writeBuffer(keyOrder[2][5+i]);
    } else if (KB.keys[2][5-i].check(true)) {
      KB.writeBuffer(keyOrder[2][5-i]);
    }
  }
}

void Keyboard::row4() {
  for (int i = 0; i < 6; i++) {
    if (KB.keys[3][5+i].check(true)) {
      KB.writeBuffer(keyOrder[3][5+i]);
    } else if (KB.keys[3][5-i].check(true)) {
      KB.writeBuffer(keyOrder[3][5-i]);
    }
  }
}

void Keyboard::writeBuffer(uint8_t c) {
  if (!_onWriteBuffer) {
    _onWriteBuffer = true;
    buffer[bufferSize++ % BUFFER] = c;
    _onWriteBuffer = false;
  }
}

Keyboard KB;
