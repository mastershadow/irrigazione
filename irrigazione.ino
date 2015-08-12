#include <DS3232RTC.h>    // http://github.com/JChristensen/DS3232RTC
#include <Time.h>         // http://www.arduino.cc/playground/Code/Time
#include <Wire.h>         // http://arduino.cc/en/Reference/Wire

/*
 * Caratteristiche
 * ===============
 * 1) Avvio automatico ciclo irrigazione a una data ora, da clock e con rilevamento pioggia (se piove non parte)
 * 2) Avvio manuale ciclo irrigazione
 * 3) Stop manuale irrigazione
 * 
 * Desiderata
 * ==========
 * - Bottone manuale: avvia su elettrovalvola N fino a stop manuale irrigazione (nuova pressione passa a N+1, se N+1 > 5 va alla 0)
 * 
 * Loop funzionamento
 * ==================
 * if (stopIsPressed())
 * - stopEverything();
 * 
 * if (hasToStartWateringLoop())
 * - startWateringLoop();
 * 
 * if (manualWateringRequested())
 * - enableManualWatering();
 * 
 * if (isWateringLoopRunning())
 * - if (hasCurrentSprinklerDone())
 * --- stopCurrentSprinkler();
 * --- if (isWateringLoopDone())
 * ------ stopEverything();
 * --- else
 * ------ startNextSprinkler();
 * 
 * Condizioni di avvio ciclo irrigazione
 * =====================================
 * Tutte le condizioni devono essere verificate:
 * - Irrigazione non avviata (ciclo o forzatura manuale)
 * - Sensore pioggia non scattato
 * - Tempo corrente > tempo avvio programmato (distinguere i giorni)
 * - Irrigazione automatica abilitata
 * - Bottone manuale non premuto (*)
 * 
 * Funzionamento ciclo irrigazione
 * ===============================
 * Ogni irrigatore deve rimanere aperto per WATERING_TIME
 * 
 */

const long WATERING_TIME = 3600000; // 60 * 60 * 1000 = 1 ora
const int PIN_SPRINKLERS[] = { 2, 3, 4, 5, 6, 7 }; // PD2 - PD7
const int PIN_RAIN_VA = 14; // A0 - PC7
const int PIN_RAIN_SW = 8; // PB0 

const int PIN_BUTTON_START = 9;
const int PIN_BUTTON_STOP = 10;
const int PIN_BUTTON_MANUAL = 11;

const int DEBOUNCE_DELAY = 50;

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
} inputs;

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_SPRINKLERS[0], OUTPUT);
  pinMode(PIN_SPRINKLERS[1], OUTPUT);
  pinMode(PIN_SPRINKLERS[2], OUTPUT);
  pinMode(PIN_SPRINKLERS[3], OUTPUT);
  pinMode(PIN_SPRINKLERS[4], OUTPUT);
  pinMode(PIN_SPRINKLERS[5], OUTPUT);

  pinMode(PIN_RAIN_VA, INPUT);
  pinMode(PIN_RAIN_SW, INPUT);
  
  pinMode(PIN_BUTTON_START, INPUT);
  pinMode(PIN_BUTTON_STOP, INPUT);
  pinMode(PIN_BUTTON_MANUAL, INPUT);
  
  setSyncProvider(RTC.get);

  memset(&inputs, 0, sizeof(Inputs));
  inputs.startState = LOW;
  inputs.stopState = LOW;
  inputs.manualState = LOW;
  inputs.startLastState = LOW;
  inputs.stopLastState = LOW;
  inputs.manualLastState = LOW;
}

void loop() {
  readInputs();
}

/* 
 * INPUT READING 
 * =================
 * Read all button states using debouncing
 * 
 */
void readInputs() {
  readStartButton();
  readStopButton();
  readManualButton();

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
    // if the button state has changed:
    if (reading != inputs.startState) {
      inputs.startState = reading;
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
    }
  }
  // save the reading
  inputs.stopLastState = reading;
}

void readManualButton() {
  int reading = digitalRead(PIN_BUTTON_STOP);
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
    }
  }
  // save the reading
  inputs.manualLastState = reading;
}

void readRainSensor() {
  // TODO
}

