#include <MIDI.h>
#include "MCP_DAC.h"

// define INPUT and OUTPUT Pins

// define Poientiometer Inputs
// Arpeggiator type, chooses the matrix type
#define A_IN_ARPEGGIATOR_TYPE A1
// Arpeggiator Gate Length, determines the Gate lenght in dependence of the Arpeggiator speed time
#define A_IN_ARPEGGIATOR_GATE A6
// if there is no clock in put, the speed will be set with this poti
#define A_IN_ARPEGGIATOR_SPEED A0

// define digitalInputs
#define D_IN_TRIG 2
#define D_IN_ARPEGGIATOR_OR_SINGLE 4
#define D_IN_NOTE_PRIORITY_1 7
#define D_IN_NOTE_PRIORITY_2 6
#define D_IN_SEQUENCER 5
#define D_IN_MODE 8

// define digitalOutputs
#define D_OUT_GATE 3
#define D_OUT_DAC_1 9

#define DEBUG true
#define SEMITONE_VOLTAGE 86

// define MIDI Control Channels
#define WHEEL 1
#define SUSTAIN_PEDAL 64





int thePressedKeyBuffer[] = { 0, 12, 24, 28, 31, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int theNoteBuffer[] = { 5, 12, 24, 28, 31, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int theUpBuffer[] = { 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int theDownBuffer[] = { 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
bool bufferUpChanged;
bool bufferDownChanged;
bool bufferChanged;
int notePointer = 0;
int matrixPointer = 0;

#define MAX_SIZE_NOTEBUFFER 20
#define MATRIX_COUNT 6

int theMatrix[][10] = { { -1, 8, 1, 1, 1, 1, -2, 1, 1, -4 },
                        { -1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
                        { -1, 2, 2, -1, 0, 0, 0, 0, 0, 0 },
                        { -1, 3, 2, 2, -3, 0, 0, 0, 0, 0 },
                        { -1, 2, 0, 1, 0, 0, 0, 0, 0, 0 },
                        { -1, 1, -1, 0, 0, 0, 0, 0, 0, 0 } };


int currentNote = -1;
int currentMidiBend = 0;
int bendRange = 2;
int arpeggiatorTrigInMillis;
int arpeggiatorGateRatio;

long lastArpAttackMs = 0;

bool clockMode = false;
bool arpeggioHoldMode = false;
bool sustainPedal = false;
#define EXPRESSION_MODE_VELOCITY  0
#define EXPRESSION_MODE_AFTERTOUCH  1
#define EXPRESSION_MODE_WHEEL  2

byte expressionMode = EXPRESSION_MODE_VELOCITY;
int currentExpression = 0;

MIDI_CREATE_DEFAULT_INSTANCE();

void setup() {
  pinMode(D_IN_ARPEGGIATOR_OR_SINGLE, INPUT);
  pinMode(D_IN_NOTE_PRIORITY_1, INPUT);  // 0 "normal mode" // 1 -> spcial modes
  pinMode(D_IN_NOTE_PRIORITY_2, INPUT);  //
  pinMode(D_IN_SEQUENCER, INPUT);
  pinMode(D_IN_MODE, INPUT_PULLUP);
  pinMode(D_IN_TRIG, INPUT_PULLUP);

  pinMode(D_OUT_DAC_1, OUTPUT);
  pinMode(D_OUT_GATE, OUTPUT);

  digitalWrite(D_OUT_GATE, LOW);

  SPI.begin();
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.setHandleAfterTouchChannel(handleAfterTouchChannel);
  initialize();
}

void initialize() {
  theNoteBuffer[0] = 0;
  notePointer = 0;
  matrixPointer = 0;
  setGateOff();
  setCurrentNote(0);
  setCurrentExpression(0);
  setBitchPend(0);
  clockMode = false;
  bufferUpChanged = true;
  bufferDownChanged = true;
}
void handleNoteOn(byte inChannel, byte inNote, byte inVelocity) {
  if (expressionMode == EXPRESSION_MODE_VELOCITY) {
    setCurrentExpression(inVelocity);
  }
  addNoteToBuffer(inNote - 21);
}

void handleNoteOff(byte inChannel, byte inNote, byte inVelocity) {
  removeNoteFromBuffer(inNote - 21);
}
void handlePitchBend(byte channel, int bend) {
  setBitchPend(bend);
}

void handleControlChange(byte inChannel, byte number, byte value) {
  if (number == SUSTAIN_PEDAL) {
    if (value == 0) {
        sustainPedal = false;
        if (isSettingMode()) {
          arpeggioHoldMode = false;
        }
    } else {
      sustainPedal = true;
      if (isSettingMode()) {
        arpeggioHoldMode = true;
      }
    }
  } else if (number == WHEEL) {
    if (isSettingMode() && value > 48) {
      expressionMode = EXPRESSION_MODE_WHEEL;
    }
    if (expressionMode == EXPRESSION_MODE_WHEEL) {
      setCurrentExpression(value);
    }
  }
}

void handleAfterTouchChannel(byte channel, byte pressure) {
  if (isSettingMode() && pressure > 48) {
    expressionMode = EXPRESSION_MODE_AFTERTOUCH;
  }
  if (expressionMode == EXPRESSION_MODE_AFTERTOUCH) {
    setCurrentExpression(pressure);
  }
}


void loop() {
  MIDI.read();
  handleMusic();
}

void handleMusic() {
  if (isArpeggiatorMode()) {
    handleArpeggiator();
  } else {
    handleSingleNote();
  }
}

boolean isArpeggiatorMode() {
  return digitalRead(D_IN_ARPEGGIATOR_OR_SINGLE);
}

boolean isSettingMode() {
  return !digitalRead(D_IN_MODE);
}

void handleSingleNote() {
  int nextNoteToPlay = getNextSingleNoteToPlay();
  setCurrentNote(nextNoteToPlay);
  if (currentNote == -1) {
    setGateOff();
  } else {
    setGateOn();
  }
}

int *getSelectedNoteBuffer() {
  if (isArpeggiatorMode()) {
    if (!digitalRead(D_IN_NOTE_PRIORITY_1)) {
      return getUpBuffer();
    } else {
      if (digitalRead(D_IN_NOTE_PRIORITY_2)) {
        return getDownBuffer();
      } else {
        return theNoteBuffer;
      }
    }
  } else {
    if (!digitalRead(D_IN_NOTE_PRIORITY_1)) {
      return theNoteBuffer;
    } else {
      if (digitalRead(D_IN_NOTE_PRIORITY_2)) {
        return getDownBuffer();
      } else {
        return getUpBuffer();
      }
    }
  }
}

int *getDownBuffer() {
  if (bufferDownChanged) {
    for (int i = 0; i <= theNoteBuffer[0]; ++i) {
      theDownBuffer[i] = theNoteBuffer[i];
    }

    qsort(theDownBuffer + 1, theNoteBuffer[0], 2, sortDesc);
    bufferDownChanged = false;
  }
  return theDownBuffer;
}

int *getUpBuffer() {
  if (true || bufferUpChanged) {
    theUpBuffer[0] = theNoteBuffer[0];
    for (int i = 0; i <= theNoteBuffer[0]; ++i) {
      theUpBuffer[i] = theNoteBuffer[i];
    }
    qsort(theUpBuffer + 1, theNoteBuffer[0], 2, sortAsc);
    bufferUpChanged = false;
  }
  return theUpBuffer;
}

int sortDesc(const void *cmp1, const void *cmp2) {
  int a = *((int *)cmp1);
  int b = *((int *)cmp2);
  return b - a;
}

int sortAsc(const void *cmp1, const void *cmp2) {
  int a = *((int *)cmp1);
  int b = *((int *)cmp2);
  return a - b;
}

int getNextSingleNoteToPlay() {
  int *noteBuffer = getSelectedNoteBuffer();
  if (noteBuffer[0] == 0) return -1;
  else {
    return noteBuffer[noteBuffer[0]];
  }
}

int *getSelectedMatrix() {
  int matrix = (analogRead(A_IN_ARPEGGIATOR_TYPE) * MATRIX_COUNT) / 1024;
  matrix %= MATRIX_COUNT;
  return theMatrix[matrix];
}

boolean isNextArpNoteToBePlayed() {
  return millis() - lastArpAttackMs > arpeggiatorTrigInMillis;
}

boolean isArpNoteOtBeSetOff() {
  return (millis() - lastArpAttackMs > (arpeggiatorTrigInMillis / 8) * (arpeggiatorGateRatio / 8) / 16);
}

void handleArpeggiator() {
  arpeggiatorTrigInMillis = analogRead(A_IN_ARPEGGIATOR_SPEED);
  arpeggiatorGateRatio = analogRead(A_IN_ARPEGGIATOR_GATE);
  if (isNextArpNoteToBePlayed()) {
    int noteToPlay = nextArpeggiatorNote(getSelectedNoteBuffer(), getSelectedMatrix());
    setCurrentNote(noteToPlay);
    
    lastArpAttackMs = millis();
    if (currentNote > -1) {
      setGateOn();
    }
  }
  if (isArpNoteOtBeSetOff()) {
    setGateOff();
  }
}


void addNoteToBuffer(int note) {
  removeNoteFromBuffer(note);  // for more resilience
  if (thePressedKeyBuffer[0] < MAX_SIZE_NOTEBUFFER) {
    bufferUpChanged = true;
    bufferDownChanged = true;
    ++thePressedKeyBuffer[0];
    thePressedKeyBuffer[thePressedKeyBuffer[0]] = note;
    if (thePressedKeyBuffer[0] == 1) {
      theNoteBuffer[0] = 0;
    }
    ++theNoteBuffer[0];
    theNoteBuffer[theNoteBuffer[0]] = note;
  }
}

void removeNoteFromBuffer(int note) {
  bufferUpChanged = true;
  bufferDownChanged = true;

  int index = 1;
  for (; index <= thePressedKeyBuffer[0]; ++index) {
    if (thePressedKeyBuffer[index] == note) {
      for (; index < thePressedKeyBuffer[0]; ++index) {
        thePressedKeyBuffer[index] = thePressedKeyBuffer[index + 1];
      }
      --thePressedKeyBuffer[0];
    }
  }
  if (!digitalRead(D_IN_ARPEGGIATOR_OR_SINGLE) || !arpeggioHoldMode) {
    for (int i = 0; i <= thePressedKeyBuffer[0]; ++i) {
      theNoteBuffer[i] = thePressedKeyBuffer[i];
    }
  }
}

int nextArpeggiatorNote(int *noteBuffer, int *matrix) {
  if (noteBuffer[0] <= 0) {
    return -1;
  }
  notePointer %= noteBuffer[0];
  int nextNote = noteBuffer[1 + notePointer];

  if (matrixPointer >= matrix[1]) {
    matrixPointer = 0;
  }
  int nextIndex = matrix[matrixPointer + 2];
  notePointer += noteBuffer[0] + nextIndex;
  matrixPointer += 1;

  return nextNote;
}

void setGateOff() {
  if (DEBUG) Serial.print("GATE OFF");
  digitalWrite(D_OUT_GATE, LOW);
}

void setGateOn() {
  if (DEBUG) Serial.println("GATE ON");
  digitalWrite(D_OUT_GATE, HIGH);
}

void setCurrentExpression(int expression) {
  if (currentExpression != expression) {
    currentExpression = expression;
    setVoltage(D_OUT_DAC_1,1,1, currentExpression * 32);
  }
}

void setBitchPend(int pitchBend) {
  if (currentMidiBend != pitchBend) {
    currentMidiBend = pitchBend;
    adjustNote();
  }
}

void setCurrentNote(int noteToPlay) {
  if (noteToPlay != currentNote) {
      currentNote = noteToPlay;
      adjustNote();
  } 
}
  

void adjustNote() {
  if (currentNote < 0) {
    setGateOff();
  } else {
    if (currentNote > 48) {
      currentNote = 48;
    }
    int voltage = currentNote * SEMITONE_VOLTAGE;
    voltage += (currentMidiBend * bendRange * SEMITONE_VOLTAGE) / 64;
    setVoltage(D_OUT_DAC_1, 0, 1, voltage);
    if (DEBUG) {
      for (int i = 0; i <= theNoteBuffer[0]; ++i) {
        Serial.print(theNoteBuffer[i]);
        Serial.print(" ");
      }
      Serial.println(currentNote);
    }
  }
}



void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV) {
  unsigned int command = channel ? 0x9000 : 0x1000;

  command |= gain ? 0x0000 : 0x2000;
  command |= (mV & 0x0FFF);

  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(dacpin, LOW);
  SPI.transfer(command >> 8);
  SPI.transfer(command & 0xFF);
  digitalWrite(dacpin, HIGH);
  SPI.endTransaction();
}
