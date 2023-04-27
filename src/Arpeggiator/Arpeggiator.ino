#include <MIDI.h>
#include <SPI.h>

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

#define DEBUG false
#define SEMITONE_VOLTAGE 86

// define some MIDI Constants
#define WHEEL 1
#define SUSTAIN_PEDAL 64

// define some constant values
#define EXPRESSION_MODE_VELOCITY 0
#define EXPRESSION_MODE_AFTERTOUCH 1
#define EXPRESSION_MODE_WHEEL 2

#define SEQUENCER_OFF 0
#define SEQUENCER_MODE_RECORD 1
#define SEQUENCER_MODE_PLAY 2
#define SEQUENCER_MODE_STOP 3
#define SEQUENCVER_MODE_OVERWRITE 4


struct NANO_PINS {
  bool isConfigMode;
  bool isArpeggiatorMode;
  bool isSequencerMode;
  bool isSettingMode;
  bool isTP_1;
  bool isTP_2;
  int arpeggiatorSpeed;
  int arpeggiatorGateRatio;
  int argeggioType;
  int clockIn;
};

struct CLOCK {
  bool currentClock;
  bool clockMode;
  long lastClockSignal;
  int clockDuration;
  int clockCounter;
};


struct SEQUENCER {
  int pointer;
  int length;
  int buffer[128];
  int shift;
  int mode;
};


struct ARPEGGIATOR {
  NANO_PINS nanoPins;
  CLOCK clock;
  SEQUENCER sequencer;
  long currentTime;
  int nextGateLength;
  long lastAutomaticNoteAttackMs;
  int currentNote;
  int currentMidiBend;
  int bendRange;
  byte expressionMode;
  byte currentExpression;
  bool arpeggioHoldMode;
  bool sustainPedal;
};



ARPEGGIATOR arpeggiator;

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
  resetClock(arpeggiator.clock);
  arpeggiator.bendRange = 2;
  arpeggiator.currentMidiBend = 0;
  arpeggiator.expressionMode = EXPRESSION_MODE_VELOCITY;
  arpeggiator.currentExpression = 0;
  bufferUpChanged = true;
  bufferDownChanged = true;
}
void handleNoteOn(byte inChannel, byte inNote, byte inVelocity) {
  if (arpeggiator.nanoPins.isConfigMode) {
    if (thePressedKeyBuffer[0] == 1) {
      arpeggiator.bendRange = abs(inNote - thePressedKeyBuffer[1]);
    } else {
      arpeggiator.expressionMode = EXPRESSION_MODE_VELOCITY;
    }
  }
  if (arpeggiator.expressionMode == EXPRESSION_MODE_VELOCITY) {
    setCurrentExpression(inVelocity);
  }

  if (arpeggiator.sequencer.mode != SEQUENCER_OFF) {
    if (arpeggiator.sequencer.mode == SEQUENCER_MODE_RECORD) {
      addNoteToSequencer(inNote);
      setCurrentNote(inNote);
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_PLAY) {
      arpeggiator.sequencer.shift = inNote;
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_STOP) {
      arpeggiator.sequencer.shift = inNote;
      arpeggiator.sequencer.mode = SEQUENCER_MODE_PLAY;
    } else if (arpeggiator.sequencer.mode == SEQUENCVER_MODE_OVERWRITE) {
      replaceNoteInSequencer(inNote);
    }
  } else {
    addNoteToBuffer(inNote);
  }
}

void addNoteToSequencer(int inNote) {
  arpeggiator.sequencer.buffer[arpeggiator.sequencer.length++] = inNote;
}
void replaceNoteInSequencer(int inNote) {
  arpeggiator.sequencer.buffer[arpeggiator.sequencer.pointer] = inNote;
}

void handleNoteOff(byte inChannel, byte inNote, byte inVelocity) {
  if (arpeggiator.sequencer.mode != SEQUENCER_OFF) {
    if (arpeggiator.sequencer.mode == SEQUENCER_MODE_RECORD) {
      setGateOff();
    }
  } else {
    removeNoteFromBuffer(inNote);
  }
}

void handlePitchBend(byte channel, int bend) {
  setBitchPend(bend);
}

void handleControlChange(byte inChannel, byte number, byte value) {
  if (number == SUSTAIN_PEDAL) {
    if (value == 0) {
      arpeggiator.sustainPedal = false;
      if (arpeggiator.nanoPins.isConfigMode) {
        arpeggiator.arpeggioHoldMode = false;
      }
    } else {
      arpeggiator.sustainPedal = true;
      if (arpeggiator.nanoPins.isConfigMode) {
        arpeggiator.arpeggioHoldMode = true;
      }
    }
  } else if (number == WHEEL) {
    if (arpeggiator.nanoPins.isConfigMode && value > 48) {
      arpeggiator.expressionMode = EXPRESSION_MODE_WHEEL;
    }
    if (arpeggiator.expressionMode == EXPRESSION_MODE_WHEEL) {
      setCurrentExpression(value);
    }
  }
}

void handleAfterTouchChannel(byte channel, byte pressure) {
  if (arpeggiator.nanoPins.isConfigMode && pressure > 48) {
    arpeggiator.expressionMode = EXPRESSION_MODE_AFTERTOUCH;
  }
  if (arpeggiator.expressionMode == EXPRESSION_MODE_AFTERTOUCH) {
    setCurrentExpression(pressure);
  }
}


void loop() {
  arpeggiator.currentTime = millis();
  prepareNanoPins();
  prepareSequencer();
  prepareClock();
  MIDI.read();
  makeMusic();
}

void prepareNanoPins() {
  arpeggiator.nanoPins.arpeggiatorGateRatio = analogRead(A_IN_ARPEGGIATOR_GATE);
  arpeggiator.nanoPins.arpeggiatorSpeed = analogRead(A_IN_ARPEGGIATOR_SPEED);
  arpeggiator.nanoPins.argeggioType = analogRead(A_IN_ARPEGGIATOR_TYPE);
  arpeggiator.nanoPins.isArpeggiatorMode = digitalRead(D_IN_ARPEGGIATOR_OR_SINGLE);
  arpeggiator.nanoPins.isSequencerMode = digitalRead(D_IN_SEQUENCER);
  arpeggiator.nanoPins.isSettingMode = digitalRead((D_IN_MODE));
  arpeggiator.nanoPins.isTP_1 = digitalRead((D_IN_NOTE_PRIORITY_1));
  arpeggiator.nanoPins.isTP_2 = digitalRead((D_IN_NOTE_PRIORITY_2));
  arpeggiator.nanoPins.clockIn = digitalRead(D_IN_TRIG);
}

void prepareSequencer() {
  if (arpeggiator.sequencer.mode == SEQUENCER_OFF && arpeggiator.nanoPins.isSequencerMode) {
    arpeggiator.sequencer.mode = SEQUENCER_MODE_STOP;
    arpeggiator.sequencer.shift = 0;
  } else if (arpeggiator.sequencer.mode != SEQUENCER_OFF && !arpeggiator.nanoPins.isSequencerMode) {
    arpeggiator.sequencer.mode = SEQUENCER_OFF;
  }

  if (arpeggiator.nanoPins.isConfigMode && arpeggiator.sequencer.mode != SEQUENCER_OFF) {
    if (arpeggiator.sequencer.mode == SEQUENCER_MODE_STOP) {
      arpeggiator.sequencer.mode = SEQUENCER_MODE_RECORD;
      arpeggiator.sequencer.length = arpeggiator.sequencer.pointer = 0;
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_RECORD && arpeggiator.sequencer.length > 0) {
      arpeggiator.sequencer.mode = SEQUENCER_MODE_STOP;
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_PLAY) {
      arpeggiator.sequencer.mode = SEQUENCVER_MODE_OVERWRITE;
    }
  } else if (arpeggiator.sequencer.mode == SEQUENCVER_MODE_OVERWRITE) {
    arpeggiator.sequencer.mode = SEQUENCER_MODE_PLAY;
  }
}

void prepareClock() {

  if (arpeggiator.clock.currentClock != arpeggiator.nanoPins.clockIn) {
    arpeggiator.clock.clockMode = true;
    arpeggiator.clock.currentClock = arpeggiator.nanoPins.clockIn;
    // In fact the transistor that shields the nano input pin
    // inverts the clock signbal. if pin has fallen to ground the clock itself is up.
    if (!arpeggiator.clock.currentClock) {
      if (arpeggiator.clock.lastClockSignal > 0) {
        arpeggiator.clock.clockDuration = arpeggiator.currentTime - arpeggiator.clock.lastClockSignal;
      } else {
        arpeggiator.clock.clockDuration = 500;  // just guessing for the first time interval
      }
      arpeggiator.clock.lastClockSignal = arpeggiator.currentTime;
      ++arpeggiator.clock.clockCounter;
    }
  } else {
    if (arpeggiator.clock.clockMode && arpeggiator.currentTime - arpeggiator.clock.lastClockSignal > 8000) {
      resetClock(arpeggiator.clock);
    }
  }
}

void resetClock(CLOCK clock) {
  clock.clockMode = false;
  clock.lastClockSignal = -1;
  clock.clockCounter = 0;
}

void makeMusic() {
  if (arpeggiator.nanoPins.isSequencerMode) {
    handleSequencer();
  } else if (arpeggiator.nanoPins.isArpeggiatorMode) {
    handleArpeggiator();
  } else {
    handleSingleNote();
  }
}





void handleSingleNote() {
  int nextNoteToPlay = getNextSingleNoteToPlay();
  setCurrentNote(nextNoteToPlay);
  if (arpeggiator.currentNote == -1) {
    setGateOff();
  } else {
    setGateOn();
  }
}

int *getNoteBufferForArpeggio() {
    if (!arpeggiator.nanoPins.isTP_1) {
      return getUpBuffer();
    } else {
      if (arpeggiator.nanoPins.isTP_2) {
        return getDownBuffer();
      } else {
        return theNoteBuffer;
      }
    }
   
}
int *getNoteBufferForSingleNote() {
    if (!arpeggiator.nanoPins.isTP_1) {
      return theNoteBuffer;
    } else {
      if (arpeggiator.nanoPins.isTP_2) {
        return getDownBuffer();
      } else {
        return getUpBuffer();
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
  int *noteBuffer = getNoteBufferForSingleNote();
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

boolean isNextAutomaticNoteToBePlayed() {
  if (arpeggiator.clock.clockMode) {
    //             0  1.    2.    3.    4   5   6   7
    // ratios -> 1/8  1/4   1/3   1/2   1   2   4   8
    int divide = 1;
    int multiple = 8;

    if (arpeggiator.nanoPins.arpeggiatorSpeed < 1 * 128) {
      divide = 8;
      multiple = 1;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 256) {
      divide = 4;
      multiple = 1;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 3 * 128) {
      divide = 3;
      multiple = 1;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 4 * 128) {
      divide = 2;
      multiple = 1;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 5 * 128) {
      divide = 1;
      multiple = 1;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 6 * 128) {
      divide = 1;
      multiple = 2;
    } else if (arpeggiator.nanoPins.arpeggiatorSpeed < 7 * 128) {
      divide = 1;
      multiple = 4;
    }



    if (arpeggiator.clock.lastClockSignal > arpeggiator.lastAutomaticNoteAttackMs && arpeggiator.clock.clockCounter == multiple) {
      arpeggiator.clock.clockCounter = 0;
      arpeggiator.nextGateLength = ((((arpeggiator.clock.clockDuration * multiple) / divide) >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >>4;
      return true;
    } else if (divide > 1 && arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > (arpeggiator.clock.clockDuration / divide + 1)) {
      arpeggiator.nextGateLength = ((((arpeggiator.clock.clockDuration * multiple) / divide) >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >> 4;
      arpeggiator.clock.clockCounter = 0;
      return true;
    }
    return false;
  } else {
    if (arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > arpeggiator.nanoPins.arpeggiatorSpeed) {
      arpeggiator.nextGateLength = ((arpeggiator.nanoPins.arpeggiatorSpeed >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >> 4;
      return true;
    }
    return false;
  }
}

boolean isAutomaticNoteToRelease() {
  return arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > arpeggiator.nextGateLength;
}

int nextSequencerNote() {
  ++ arpeggiator.sequencer.pointer;
  if (arpeggiator.sequencer.pointer >= arpeggiator.sequencer.length) {
    arpeggiator.sequencer.pointer = 0;
  }
  return arpeggiator.sequencer.buffer[arpeggiator.sequencer.pointer];
}

void handleSequencer() {
  if (arpeggiator.sequencer.mode == SEQUENCER_MODE_PLAY || arpeggiator.sequencer.mode == SEQUENCVER_MODE_OVERWRITE) {
    if (isNextAutomaticNoteToBePlayed()) {
      setCurrentNote(nextSequencerNote() + arpeggiator.sequencer.shift);
      arpeggiator.lastAutomaticNoteAttackMs = arpeggiator.currentTime;
    }
    if (arpeggiator.currentNote > -1) {
      setGateOn();
    }
  }
  if (isAutomaticNoteToRelease()) {
    setGateOff();
  }
}

void handleArpeggiator() {
  if (isNextAutomaticNoteToBePlayed()) {
    setCurrentNote(nextArpeggiatorNote(getNoteBufferForArpeggio(), getSelectedMatrix()));
    arpeggiator.lastAutomaticNoteAttackMs = arpeggiator.currentTime;
    if (arpeggiator.currentNote > -1) {
      setGateOn();
    }
  }
  if (isAutomaticNoteToRelease()) {
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
  if ((arpeggiator.nanoPins.isArpeggiatorMode && !arpeggiator.arpeggioHoldMode) || 
      ( !arpeggiator.sustainPedal)) {
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
  if (arpeggiator.currentExpression != expression) {
    arpeggiator.currentExpression = expression;
    setVoltage(D_OUT_DAC_1, 1, 1, arpeggiator.currentExpression * 32);
  }
}

void setBitchPend(int pitchBend) {
  if (arpeggiator.currentMidiBend != pitchBend) {
    arpeggiator.currentMidiBend = pitchBend;
    adjustNote();
  }
}

void setCurrentNote(int noteToPlay) {
  if (noteToPlay != arpeggiator.currentNote) {
    arpeggiator.currentNote = noteToPlay;
    adjustNote();
  }
}


void adjustNote() {
  if (arpeggiator.currentNote < 48) {
    setGateOff();
  } else {
    if (arpeggiator.currentNote > 96) {
      arpeggiator.currentNote = 96;
    }
    int voltage = (arpeggiator.currentNote - 48) * SEMITONE_VOLTAGE;
    voltage += (arpeggiator.currentMidiBend * arpeggiator.bendRange * SEMITONE_VOLTAGE) / 64;
    setVoltage(D_OUT_DAC_1, 0, 1, voltage);
    if (DEBUG) {
      for (int i = 0; i <= theNoteBuffer[0]; ++i) {
        Serial.print(theNoteBuffer[i]);
        Serial.print(" ");
      }
      Serial.println(arpeggiator.currentNote);
    }
  }
}



void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV) {
  if (mV > 4095) {
    mV = 4095;
  }
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
