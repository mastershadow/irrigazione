#include <DS3232RTC.h>    // http://github.com/JChristensen/DS3232RTC
#include <Time.h>         // http://www.arduino.cc/playground/Code/Time
#include <Wire.h>         // http://arduino.cc/en/Reference/Wire

#define DEBUG 1

/*
   Caratteristiche
   ===============
   1) Avvio automatico ciclo irrigazione a una data ora, da clock e con rilevamento pioggia (se piove non parte)
   2) Avvio manuale ciclo irrigazione
   3) Stop manuale irrigazione

   Desiderata
   ==========
   - Bottone manuale: avvia su elettrovalvola N fino a stop manuale irrigazione (nuova pressione passa a N+1, se N+1 > 5 va alla 0)

   Loop funzionamento
   ==================
   if (isStopPushed())
   - stopEverything();

   if (hasToStartWateringLoop())
   - startWateringLoop();

   if (manualWateringRequested())
   - enableManualWatering();

   if (isWateringLoopRunning())
   - if (hasCurrentSprinklerDone())
   --- stopCurrentSprinkler();
   --- if (isWateringLoopDone())
   ------ stopEverything();
   --- else
   ------ startNextSprinkler();

   Condizioni di avvio ciclo irrigazione
   =====================================
   Tutte le condizioni devono essere verificate:
   - Irrigazione non avviata (ciclo o forzatura manuale)
   - Sensore pioggia non scattato
   - Tempo corrente > tempo avvio programmato (distinguere i giorni)
   - Irrigazione automatica abilitata
   - Bottone manuale non premuto (*)

   Funzionamento ciclo irrigazione
   ===============================
   Ogni irrigatore deve rimanere aperto per WATERING_TIME

*/

#ifdef DEBUG
#define LOG 1
#endif

// const long WATERING_TIME = 3600; // 60 * 60 = 1 hour
const long WATERING_TIME = 5;
const int PIN_SPRINKLERS[] = { 2, 3, 4, 5, 6, 7 }; // PD2 - PD7
const int PIN_RAIN_VA = A0; // A0 - PC7
const int PIN_RAIN_SW = 8; // PB0

const int PIN_BUTTON_START = 9;
const int PIN_BUTTON_STOP = 10;
const int PIN_BUTTON_MANUAL = 11;
const int PIN_SWITCH_ENABLE = 12;

const int DEBOUNCE_DELAY = 50;

// without DST so take off an hour 
const int WATERING_START_HOUR = 20; // it is 21
const int WATERING_START_MINUTE = 54;

struct Commands {
  int start;
  int stop;
  int manual;
} commands;

struct Inputs {
  int startState;
  int startLastState;
  long startDebounceTime;

  int stopState;
  int stopLastState;
  long stopDebounceTime;

  int manualState;
  int manualLastState;
  long manualDebounceTime;

  int enableState;
  int enableLastState;
  long enableDebounceTime;

  int rainValue;
  int rainState; // LOW = RAIN - HIGH = NO RAIN
  int rainLastState;
  long rainDebounceTime;

} inputs;

time_t nextWateringTime = 0;
bool wateringLoopRunning = false;
bool manualWateringRunning = false;
int currentSprinkler = -1;
long currentSprinklerStartTime = 0;


void setup() {
  setSyncProvider(RTC.get);
  Serial.begin(9600);
  // put your setup code here, to run once:
  pinMode(PIN_SPRINKLERS[0], OUTPUT);
  pinMode(PIN_SPRINKLERS[1], OUTPUT);
  pinMode(PIN_SPRINKLERS[2], OUTPUT);
  pinMode(PIN_SPRINKLERS[3], OUTPUT);
  pinMode(PIN_SPRINKLERS[4], OUTPUT);
  pinMode(PIN_SPRINKLERS[5], OUTPUT);

  pinMode(PIN_RAIN_VA, INPUT);
  pinMode(PIN_RAIN_SW, INPUT);

  pinMode(PIN_BUTTON_START, INPUT_PULLUP);
  pinMode(PIN_BUTTON_STOP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_MANUAL, INPUT_PULLUP);
  pinMode(PIN_SWITCH_ENABLE, INPUT_PULLUP);

  memset(&inputs, 0, sizeof(Inputs));
  inputs.startState = HIGH;
  inputs.startLastState = HIGH;

  inputs.stopState = HIGH;
  inputs.stopLastState = HIGH;

  inputs.manualState = HIGH;
  inputs.manualLastState = HIGH;

  inputs.enableState = HIGH;
  inputs.enableLastState = HIGH;

  inputs.rainState = LOW;
  inputs.rainLastState = LOW;
  
  memset(&commands, 0, sizeof(Commands));
  commands.start = false;
  commands.stop = false;
  commands.manual = false;

  delay(500);
  Serial.println("Time now (CET):");
  digitalClockDisplay(now());
  setNextAutomaticWatering();
  Serial.println("Setup complete.");
}

void loop() {
  readInputs();
  
  if (commands.stop) {
    stopEverything();
    commands.stop = false;
  }

  if (commands.manual) {
    if (wateringLoopRunning) {
      // go to next sprinkler but keep looping
      int nextSprinkler = getNextSprinkler();
      if (nextSprinkler != 0) {
        stopSprinkler(currentSprinkler);
        startSprinkler(nextSprinkler);
      }
    } else {
      // manual
      startManualWatering();
    }
    commands.manual = false;
  }

  if (hasToStartWateringLoop() && !manualWateringRunning  && !wateringLoopRunning) {
    startWateringLoop();
  }

  wateringLoop();
}

bool hasToStartWateringLoop() {
  // FIXME this is always true (d'oh)
  bool timerDone = now() > nextWateringTime ? true : false;
  // loop start requested by button
  if (commands.start) {
    commands.start = false;
    return true;
  }

  // loop start requested by scheduling
  if (timerDone &&
      !wateringLoopRunning &&
      !manualWateringRunning &&
      !isRaining() &&
      isAutomaticWateringEnabled()) {
#if LOG
    Serial.println("Start watering loop");
#endif
    return true;
  }
  return false;
}

void startWateringLoop() {
  stopEverything();
  wateringLoopRunning = true;
  startSprinkler(0);
}

void wateringLoop() {
  if (wateringLoopRunning == true) {
    if (now() - currentSprinklerStartTime >= WATERING_TIME) {
      stopSprinkler(currentSprinkler);
      int nextSprinkler = getNextSprinkler();
      if (nextSprinkler != 0) {
        // loop is not completed. move to next
        startSprinkler(nextSprinkler);
      } else {
        // loop completed. stop
        stopEverything();
      }
    }
  }
}

void startSprinkler(int sprinkler) {
  currentSprinkler = sprinkler;
  currentSprinklerStartTime = now();
  digitalWrite(PIN_SPRINKLERS[sprinkler], HIGH);
}

void stopSprinkler(int sprinkler) {
  digitalWrite(PIN_SPRINKLERS[sprinkler], LOW);
}

void stopSprinklers() {
  for (int i = 0; i < (int)(sizeof(PIN_SPRINKLERS)/sizeof(PIN_SPRINKLERS[0])); i++) {
    stopSprinkler(i);
  }
}

// if we are already watering in a loop, it passes to manual mode switching to the next sprinkler
void startManualWatering() {
  // this is needed to increase the sprinkler on demand and to loop
  // on the first iteration currentSprinkler is -1 and nextSprinkler gets 0 as value
  // on the following iteration it gets looped 0-5
  int nextSprinkler = getNextSprinkler();
  stopEverything();
  manualWateringRunning = true;
  startSprinkler(nextSprinkler);
}

int getNextSprinkler() {
  int next = ((currentSprinkler + 1) % (int)(sizeof(PIN_SPRINKLERS)/sizeof(PIN_SPRINKLERS[0])));
  return next;
}

bool isRaining() {
  bool rain = inputs.rainState == HIGH;
  return rain;
}

bool isAutomaticWateringEnabled() {
  return inputs.enableState == LOW;
}

void stopEverything() {
  stopSprinklers();
  wateringLoopRunning = false;
  manualWateringRunning = false;
  currentSprinkler = -1;
  currentSprinklerStartTime = 0;
  setNextAutomaticWatering();
}

void setNextAutomaticWatering() {
  tmElements_t tm;
  if (RTC.read(tm) == 0) {
    time_t curTime = makeTime(tm);
    tm.Hour = WATERING_START_HOUR;
    tm.Minute = WATERING_START_MINUTE;
    tm.Second = 0;
    // we should address DST but we dont right now so we just add a day ^_^
    time_t t = makeTime(tm);

    // if t has already passed set to the next day otherwise keep the value
    if (t < curTime) {
      nextWateringTime = t + SECS_PER_DAY;
    } else {
      nextWateringTime = t;
    }
    #if LOG
    Serial.println("Next watering at:");
    digitalClockDisplay(nextWateringTime);
    #endif
  }
}

/*
   INPUT READING
   =================
   Read all button states using debouncing

*/
void readInputs() {
  readStartButton();
  readStopButton();
  readManualButton();
  readEnableSwitch();

  readRainSensor();
}

void readStartButton() {
  int reading = digitalRead(PIN_BUTTON_START);
  // input has changed
  if (reading != inputs.startLastState) {
    // reset the debouncing timer
    inputs.startDebounceTime = millis();
  }
  // after DEBOUNCE_DELAY input should be settled down
  if ((millis() - inputs.startDebounceTime) > DEBOUNCE_DELAY) {
    // if the button state is low start has been requested:
    if (reading != inputs.startState) {
      inputs.startState = reading;
      if (inputs.startState == LOW) {
        commands.start = true;
      }
#if LOG
      Serial.print("Start changed to: ");
      Serial.println(inputs.startState, DEC);
#endif
    }
  }
  // save the reading
  inputs.startLastState = reading;
}

void readStopButton() {
  int reading = digitalRead(PIN_BUTTON_STOP);
  // input has changed
  if (reading != inputs.stopLastState) {
    // reset the debouncing timer
    inputs.stopDebounceTime = millis();
  }
  // after DEBOUNCE_DELAY input should be settled down
  if ((millis() - inputs.stopDebounceTime) > DEBOUNCE_DELAY) {
    // if the button state has changed:
    if (reading != inputs.stopState) {
      inputs.stopState = reading;
      if (inputs.stopState == LOW) {
        commands.stop = true;
      }
#if LOG
      Serial.print("Stop changed to: ");
      Serial.println(reading, DEC);
#endif
    }
  }
  // save the reading
  inputs.stopLastState = reading;
}

void readManualButton() {
  int reading = digitalRead(PIN_BUTTON_MANUAL);
  // input has changed
  if (reading != inputs.manualLastState) {
    // reset the debouncing timer
    inputs.manualDebounceTime = millis();
  }
  // after DEBOUNCE_DELAY input should be settled down
  if ((millis() - inputs.manualDebounceTime) > DEBOUNCE_DELAY) {
    // if the button state has changed:
    if (reading != inputs.manualState) {
      inputs.manualState = reading;
      if (inputs.manualState == LOW) {
        commands.manual = true;
      }
#if LOG
      Serial.print("Manual changed to: ");
      Serial.println(reading, DEC);
#endif
    }
  }
  // save the reading
  inputs.manualLastState = reading;
}

void readEnableSwitch() {
  int reading = digitalRead(PIN_SWITCH_ENABLE);
  // input has changed
  if (reading != inputs.enableLastState) {
    // reset the debouncing timer
    inputs.enableDebounceTime = millis();
  }
  // after DEBOUNCE_DELAY input should be settled down
  if ((millis() - inputs.enableDebounceTime) > DEBOUNCE_DELAY) {
    // if the button state has changed:
    if (reading != inputs.enableState) {
      inputs.enableState = reading;
#if LOG
      Serial.print("Enable changed to: ");
      Serial.println(reading, DEC);
#endif
    }
  }
  // save the reading
  inputs.enableLastState = reading;
}

void readRainSensor() {
  inputs.rainValue = analogRead(PIN_RAIN_VA);

  int reading = digitalRead(PIN_RAIN_SW);
  // input has changed
  if (reading != inputs.rainLastState) {
    // reset the debouncing timer
    inputs.rainDebounceTime = millis();
  }
  // after DEBOUNCE_DELAY input should be settled down
  if ((millis() - inputs.rainDebounceTime) > DEBOUNCE_DELAY) {
    // if the button state has changed:
    if (reading != inputs.rainState) {
      inputs.rainState = reading;
#if LOG
      Serial.print("Rain changed to: ");
      Serial.println(reading, DEC);
#endif
    }
  }
  // save the reading
  inputs.rainLastState = reading;
}

void digitalClockDisplay(time_t t) {
  // digital clock display of the time
  Serial.print(hour(t));
  printDigits(minute(t));
  printDigits(second(t));
  Serial.print(" ");
  Serial.print(day(t));
  Serial.print(" ");
  Serial.print(month(t));
  Serial.print(" ");
  Serial.print(year(t));
  Serial.println();
}

void printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}


