#include <MIDI.h>
#include <SPI.h>

// define INPUT and OUTPUT Pins

// define Poientiometer Inputs
// Arpeggiator type, chooses the matrix type
#define A_IN_ARPEGGIATOR_TYPE A1
// Arpeggiator Gate Length, determines the Gate lenght in dependence of the Arpeggiator speed time
#define A_IN_ARPEGGIATOR_GATE A0
// if there is no clock input, the speed will be set with this poti
#define A_IN_ARPEGGIATOR_SPEED A6

// define digitalInputs
#define D_IN_TRIG 2
#define D_IN_ARPEGGIATOR_OR_SINGLE 5
#define D_IN_NOTE_PRIORITY_1 7
#define D_IN_NOTE_PRIORITY_2 6
#define D_IN_SEQUENCER 4
#define D_IN_MODE 8

#define MATRIX_BEGIN 31
#define MATRIX_RANDOM 63

// define digitalOutputs
#define D_OUT_GATE 3
#define D_OUT_DAC_1 9

#define DEBUG false

// in fact SEMITONE VOltage shpuld be 85.33333.... 
// when calculating the voltage in adjustNote() I do some 
// little math to prevent float calculations
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

#define SEQUENCVER_MODE_OVERWRITE 15


struct NANO_PINS {
  bool isConfigMode;
  bool isArpeggiatorMode;
  bool isSequencerMode;
  bool isTP_1;
  bool isTP_2;
  int arpeggiatorSpeed;
  int arpeggiatorGateRatio;
  int argeggioType;
  bool clockIn;
};

struct CLOCK {
  bool currentClock;
  bool clockMode;
  long lastClockSignal;
  int clockDuration;
  byte clockCounter;
};


struct SEQUENCER {
  char pointer;
  char length;
  char buffer[128];
  char shift;
  byte mode;
  bool currentDirectionIsUp;
};


struct ARPEGGIATOR {
  NANO_PINS nanoPins;
  CLOCK clock;
  SEQUENCER sequencer;
  long currentTime;
  int nextGateLength;
  long lastAutomaticNoteAttackMs;
  char currentNote;
  int currentMidiBend;
  int bendRange;
  char expressionMode;
  byte currentExpression;
  bool arpeggioHoldMode;
  bool sustainPedal;
  bool currentGate;
};



ARPEGGIATOR arpeggiator;

char thePressedKeyBuffer[] = { 5, 60, 72, 76, 69, 54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char theNoteBuffer[] = { 5, 60, 72, 76, 69, 54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char theUpBuffer[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char theDownBuffer[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

bool bufferUpChanged;
bool bufferDownChanged;
bool bufferChanged;
char notePointer = 0;
char matrixPointer = 0;

#define MAX_SIZE_NOTEBUFFER 20
#define MATRIX_COUNT 6

char theMatrix[][10] = { { -1, 8, 1, 1, 1, 1, -2, 1, 1, -4 },
                        { -1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
                        { -1, 2, 2, -1, 0, 0, 0, 0, 0, 0 },
                        { -1, 3, 2, 2, -3, 0, 0, 0, 0, 0 },
                        { -1, 2, 0, 1, 0, 0, 0, 0, 0, 0 },
                        { -1, 1, -1, 0, 0, 0, 0, 0, 0, 0 } };




MIDI_CREATE_DEFAULT_INSTANCE();

void setup() {
  pinMode(D_IN_ARPEGGIATOR_OR_SINGLE, INPUT);
  pinMode(D_IN_NOTE_PRIORITY_1, INPUT);
  pinMode(D_IN_NOTE_PRIORITY_2, INPUT);
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
  resetBuffers();
  resetClock();
  arpeggiator.bendRange = 2;
  arpeggiator.currentMidiBend = 0;
  arpeggiator.expressionMode = EXPRESSION_MODE_VELOCITY;
  arpeggiator.currentExpression = 0;
  setVoltage(D_OUT_DAC_1, 1, 1, arpeggiator.currentExpression * 32);
}

void resetBuffers() {
  arpeggiator.sustainPedal = false;
  theNoteBuffer[0] = 0;
  thePressedKeyBuffer[0] = 0;
  notePointer = 0;
  matrixPointer = 0;
  setBitchPend(0);
  setGateOff();
  setCurrentNote(0);
  setCurrentExpression(0);
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



  if (arpeggiator.sequencer.mode != SEQUENCER_OFF) {
    if (arpeggiator.sequencer.mode == SEQUENCER_MODE_RECORD) {
      addNoteToSequencer(inNote);
      setCurrentNote(inNote);
      setGateOn();
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_PLAY) {
      arpeggiator.sequencer.shift = inNote - 60;
    } else if (arpeggiator.sequencer.mode == SEQUENCER_MODE_STOP) {
      arpeggiator.sequencer.shift = inNote - 60;
      arpeggiator.sequencer.mode = SEQUENCER_MODE_PLAY;
    } else if (arpeggiator.sequencer.mode == SEQUENCVER_MODE_OVERWRITE) {
      replaceNoteInSequencer(inNote);
    }
  } else {
    if (arpeggiator.expressionMode == EXPRESSION_MODE_VELOCITY) {
      setCurrentExpression(inVelocity);
    }
    addNoteToBuffer(inNote);
  }
}

void addNoteToSequencer(int inNote) {
  if (arpeggiator.sequencer.length < 127) {
    arpeggiator.sequencer.buffer[arpeggiator.sequencer.length++] = inNote;
  }
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
  arpeggiator.nanoPins.isConfigMode = !digitalRead((D_IN_MODE));
  arpeggiator.nanoPins.isTP_1 = digitalRead((D_IN_NOTE_PRIORITY_1));
  arpeggiator.nanoPins.isTP_2 = digitalRead((D_IN_NOTE_PRIORITY_2));
  arpeggiator.nanoPins.clockIn = digitalRead(D_IN_TRIG);
  if (DEBUG) {
    Serial.print(arpeggiator.nanoPins.arpeggiatorGateRatio);
    Serial.print("\t");
    Serial.print(arpeggiator.nanoPins.arpeggiatorSpeed);
    Serial.print("\tat ");
    Serial.print(arpeggiator.nanoPins.argeggioType);
    Serial.print("\tam ");
    Serial.print(arpeggiator.nanoPins.isArpeggiatorMode);
    Serial.print("\tsm ");
    Serial.print(arpeggiator.nanoPins.isSequencerMode);
    Serial.print("\tmo ");
    Serial.print(arpeggiator.nanoPins.isConfigMode);
    Serial.print("\ttp1 ");
    Serial.print(arpeggiator.nanoPins.isTP_1);
    Serial.print("\ttp2 ");
    Serial.print(arpeggiator.nanoPins.isTP_2);
    Serial.print("\tci ");
    Serial.print(arpeggiator.nanoPins.clockIn);
    Serial.print("\tsm ");
    Serial.print(arpeggiator.sequencer.mode);
    Serial.print("\tcm ");
    Serial.print(arpeggiator.clock.clockMode);
    Serial.print("\tct ");
    Serial.print(arpeggiator.currentTime);
    Serial.print("\tlast ");
    Serial.print(arpeggiator.lastAutomaticNoteAttackMs);
    Serial.print("\tcn ");
    Serial.print(arpeggiator.currentNote);
    Serial.print("\tcg ");
    Serial.print(arpeggiator.currentGate);
    Serial.print("\tsus ");
    Serial.print(arpeggiator.sustainPedal);
    Serial.print("\thm ");
    Serial.print(arpeggiator.arpeggioHoldMode);
    Serial.print("\tce ");
    Serial.print(arpeggiator.currentExpression);
    Serial.println();
  }
}

void prepareSequencer() {
  if (arpeggiator.sequencer.mode == SEQUENCER_OFF && arpeggiator.nanoPins.isSequencerMode) {
    resetBuffers();
    arpeggiator.sequencer.mode = SEQUENCER_MODE_STOP;
    arpeggiator.sequencer.shift = 0;
    if (arpeggiator.nanoPins.isConfigMode) {
      arpeggiator.sequencer.length = 0;
    }
  } else if (arpeggiator.sequencer.mode != SEQUENCER_OFF && !arpeggiator.nanoPins.isSequencerMode) {
    arpeggiator.sequencer.mode = SEQUENCER_OFF;
  }

  if (arpeggiator.nanoPins.isConfigMode && arpeggiator.sequencer.mode != SEQUENCER_OFF) {
    if (arpeggiator.sequencer.mode == SEQUENCER_MODE_STOP && arpeggiator.sequencer.length == 0) {
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
      resetClock();
    }
  }
}

void resetClock() {
  arpeggiator.clock.clockMode = false;
  arpeggiator.clock.lastClockSignal = -1;
  arpeggiator.clock.clockCounter = 0;
  arpeggiator.clock.currentClock = arpeggiator.nanoPins.clockIn;
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

char *getNoteBufferForArpeggio() {
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

char *getNoteBufferForSingleNote() {
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


char *getDownBuffer() {
  if (bufferDownChanged) {
    for (int i = 0; i <= theNoteBuffer[0]; ++i) {
      theDownBuffer[i] = theNoteBuffer[i];
    }

    qsort(theDownBuffer + 1, theNoteBuffer[0], 1, sortCharsDesc);
    bufferDownChanged = false;
  }
  return theDownBuffer;
}

char *getUpBuffer() {
  if (bufferUpChanged) {
    theUpBuffer[0] = theNoteBuffer[0];
    for (int i = 0; i <= theNoteBuffer[0]; ++i) {
      theUpBuffer[i] = theNoteBuffer[i];
    }
    qsort(theUpBuffer + 1, theNoteBuffer[0], 1, sortCharssAsc);
    bufferUpChanged = false;
  }
  return theUpBuffer;
}

int sortCharsDesc(const void *cmp1, const void *cmp2) {
  char a = *((char *)cmp1);
  char b = *((char *)cmp2);
  return b - a;
}

int sortCharssAsc(const void *cmp1, const void *cmp2) {
  char a = *((char *)cmp1);
  char b = *((char *)cmp2);
  return a - b;
}

char getNextSingleNoteToPlay() {
  char *noteBuffer = getNoteBufferForSingleNote();
  if (noteBuffer[0] == 0) return -1;
  else {
    return noteBuffer[noteBuffer[0]];
  }
}

char *getSelectedMatrix() {
  int matrix = (arpeggiator.nanoPins.argeggioType * MATRIX_COUNT) / 1024;
  matrix %= MATRIX_COUNT;
  return theMatrix[matrix];
}

char * getSelectedSequencer() {
  int sequencer = (arpeggiator.nanoPins.argeggioType * 4) / 1024;
  sequencer %= 4;
  return arpeggiator.sequencer.buffer;
}



boolean isNextAutomaticNoteToBePlayed() {
  if (arpeggiator.clock.clockMode) {
    //             0  1.    2.    3.    4   5   6   7
    // ratios -> 1/8  1/4   1/3   1/2   1   2   4   8
    byte divide = 1;
    byte multiple = 8;

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
      arpeggiator.nextGateLength = ((((arpeggiator.clock.clockDuration * multiple) / divide) >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >> 4;
      return true;
    } else if (divide > 1 && arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > (arpeggiator.clock.clockDuration / divide + 1)) {
      arpeggiator.nextGateLength = ((((arpeggiator.clock.clockDuration * multiple) / divide) >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >> 4;
      arpeggiator.clock.clockCounter = 0;
      return true;
    }
    return false;
  } else {
    int speed = arpeggiator.nanoPins.arpeggiatorSpeed * 4;
    speed = speed < 10 ? 10 : speed;
    if (arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > speed) {
      arpeggiator.nextGateLength = ((speed >> 3) * (arpeggiator.nanoPins.arpeggiatorGateRatio >> 3)) >> 4;
      return true;
    }
    return false;
  }
}

boolean isAutomaticNoteToRelease() {
  return arpeggiator.currentTime - arpeggiator.lastAutomaticNoteAttackMs > arpeggiator.nextGateLength;
}

char nextSequencerNote() {
  // normal sequencer direction
  if (!arpeggiator.nanoPins.isTP_1) { 
    arpeggiator.sequencer.currentDirectionIsUp = true;
    ++ arpeggiator.sequencer.pointer;
  } else { 
    // simply backwards playing
    if (arpeggiator.nanoPins.isTP_2) {
      arpeggiator.sequencer.currentDirectionIsUp = false;
      -- arpeggiator.sequencer.pointer;
    } else { // forward and backward 
      if (arpeggiator.sequencer.currentDirectionIsUp) {
        if (arpeggiator.sequencer.pointer == arpeggiator.sequencer.length - 1) {
          arpeggiator.sequencer.currentDirectionIsUp = false;
        } else {
          ++ arpeggiator.sequencer.pointer;     
        }
      } else {
        if (arpeggiator.sequencer.pointer == 0) {
          arpeggiator.sequencer.currentDirectionIsUp = true;
        } else {
          -- arpeggiator.sequencer.pointer;
        }
      }
    }
  }
  if (arpeggiator.sequencer.pointer >= arpeggiator.sequencer.length) {
      arpeggiator.sequencer.pointer = 0;
  }
  if (arpeggiator.sequencer.pointer < 0) {
        arpeggiator.sequencer.pointer = arpeggiator.sequencer.length -1;
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


void addNoteToBuffer(char note) {
  removeNoteFromBuffer(note);  // for more resilience
  if (thePressedKeyBuffer[0] < MAX_SIZE_NOTEBUFFER) {
    bufferUpChanged = true;
    bufferDownChanged = true;
    ++thePressedKeyBuffer[0];
    thePressedKeyBuffer[thePressedKeyBuffer[0]] = note;
    if (thePressedKeyBuffer[0] == 1) {
      theNoteBuffer[0] = 0;
      // start the eqeunce or arpeggio immediately, but only if not in 
      // clockmode or arpeggioHoldMode
      if (!arpeggiator.clock.clockMode && !arpeggiator.arpeggioHoldMode){
              arpeggiator.lastAutomaticNoteAttackMs = 0;
      }
    }
    ++theNoteBuffer[0];
    theNoteBuffer[theNoteBuffer[0]] = note;
  }
}

void removeNoteFromBuffer(char note) {
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
  if (!(
        (arpeggiator.nanoPins.isArpeggiatorMode && arpeggiator.arpeggioHoldMode) || (!arpeggiator.nanoPins.isArpeggiatorMode && arpeggiator.sustainPedal))) {
    for (int i = 0; i <= thePressedKeyBuffer[0]; ++i) {
      theNoteBuffer[i] = thePressedKeyBuffer[i];
    }
  }
}

char nextArpeggiatorNote(char *noteBuffer, char *matrix) {
  if (noteBuffer[0] <= 0) {
    return -1;
  }
  notePointer %= noteBuffer[0];
  int nextNote = noteBuffer[1 + notePointer];

  if (matrixPointer >= matrix[1]) {
    matrixPointer = 0;
  }
  int nextIndex = matrix[matrixPointer + 2];
  if (nextIndex == MATRIX_BEGIN) {
    notePointer = 0;
  } else if (nextIndex == MATRIX_RANDOM) {
    notePointer = rand();
  } else {
    notePointer += noteBuffer[0] + nextIndex;
  }
  matrixPointer += 1;

  return nextNote;
}

void setGateOff() {
  arpeggiator.currentGate = false;
  digitalWrite(D_OUT_GATE, LOW);
}

void setGateOn() {
  arpeggiator.currentGate = true;
  digitalWrite(D_OUT_GATE, HIGH);
}

void setCurrentExpression(byte expression) {
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

void setCurrentNote(char noteToPlay) {
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
    // correct SEMITONE Voltage is 85.3333
    // doing some simple maths to avoid possible slower float caluclations
    // vltage 0 should start at MidiNote 48
    int voltage = (arpeggiator.currentNote-48) * (SEMITONE_VOLTAGE -2);
    voltage -= (arpeggiator.currentNote-48  )/ 3;
    //voltage += (arpeggiator.currentMidiBend * arpeggiator.bendRange * SEMITONE_VOLTAGE) / 64;
    //voltage -= 4096;
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
