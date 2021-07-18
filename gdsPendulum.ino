/************************************
  gdsPendulum
  My project to monitor & control a grandfather clock built for me by my father.  It's been running
  for many years and is starting to show wear in the timekeeping mechanism and the chimes.
  I use an arduino to influence the clock to maintain the correct time within
  less than a second. Typically, it drifts within a 100 millisecond range.

  The monitor uses a blue LED and a light sensor mounted near one end of the pendulum swing to
  measure the period and amplitude of the swing.

  The controller uses a coil from a relay mounted underneath the pendulum at bottom dead center
  acting as an electromagnet to increase "gravity"  which speeds up the clock slightly.
  If the clock is set slightly slow, we can encourage it to tick-tock in sync with a 1pps signal from a GPS.

  Pendulums are complicated! The pendulum on this clock has a period of 1.81818 seconds (surprise surprise!)
  and beats 66 times per minute.  I sure learned a lot about them on this project.
  https://en.wikipedia.org/wiki/Pendulum

  26 March 2017 - gswann
  Using a test pendulum for control testing
  29 March 2017 - gswann
  now connected to real grandfather clock
  added GPS with 1 pps signal to synchronize the clock
  5 Apr 2017
  added an Adafruit 1.2 inch 7 segment display
  to show the grandfather clock time on a digital
  display. How cool is that?
  7 Apr 2017:   working for a better start up that isn't so touchy to sync with the GPS
  12 Apr 2017:  This version works very well for time keeping
  16 Apr 2017:  The clock seems to stop sometimes, so adding timeouts for pendulum swing
  29 Apr 2017:  Adding version number and display
  1 May 2017:   the clock mechanism is old and binds sometimes. If the pendulum amplitude starts falling
                off, we use a timed pulse to the electromagnet to add some energy to the swing - works!
                The clock no longer stops.
                Since the chiming mechanism is worn, I added a MIDI player to chime as Big Ben does.
  11 May 2017:  Added reset at 1:05 AM for the MIDI controller.  It starts messing up after awhile.
  20 May 2017:  Changed counts_millis to unsigned long. Was causing a problem with delta calculation 
                after several days
   4 Oct 2017:  Adding PIR sensor to reduce the chime volume when someone is in the room.   
  20 Nov 2017:  added delta offset to allow fine tuning of clock delta to GPS
  
  The music player used for the big Ben chimes:
  https://www.sparkfun.com/products/10587

  The Adafruit folks are great with their products and their tutorials
  This gave me the start to get the notes to play independently of
  the intensive pendulum monitoring and control code:
  https://learn.adafruit.com/multi-tasking-the-arduino-part-1/ditch-the-delay

  Their display shows hours and minutes:
  https://www.adafruit.com/product/1268
  https://learn.adafruit.com/adafruit-led-backpack
  The display brightness is varied depending on the time of day.

  20 Apr 2020: Increased error limit to <2   see below in C O N T R O L

   16 Jul 2021 - set up rp1 to compile and upload the program. 
    Incrementing version number to test. 
    Changed to 24 hour clock near line 359

 ************************************/

#define VERSION 1013
#include <Wire.h> // Enable this line if using Arduino Uno, Mega, etc. Is I2C with addresses
// see https://www.arduino.cc/en/Reference/Wire
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <SoftwareSerial.h>

Adafruit_7segment matrix = Adafruit_7segment();

#define ARRSIZE 20
#define APM 10

// define the pins
byte pps = 2;         // 1pps from Motorola Oncore
byte PIR = 5;         // input from PIR sensor
byte ledCenter = 6;   // blink when pendulum crosses center point
byte ledERR = 7;      // high is green, low is red (error condition)
byte SW = 8;          // TEPT5600 light sensor
byte COIL = 12;       // Core from a 24V relay to speed up the pendulum
byte resetMIDI = 4;   // Tied to VS1053 Reset line
byte ledBlink = 13;
byte note = 0;        //The MIDI note value to be played
int  instrument = 15; // tubular bell
int volume = 60;
SoftwareSerial mySerial(5, 3); // RX, TX   only TX is used to send data to MIDI controller, RX is not used

// big ben
// https://en.wikipedia.org/wiki/Westminster_Quarters
int chime[] = {
  68, 66, 64, 59, 64, 68, 66, 59, 64, 66, 68, 64, 68, 64, 66, 59, 59, 66, 68, 64, 00
};

// array to hold notes being played
int set[2][40];
byte setptr = 99;
long setmillis = 0;

int brightness = 15;
char bmode = 'a';
int a;
int readPIR; 
unsigned int delay_val;
double ctr;
int period;
float avgPeriod;
int arrPeriod[ARRSIZE];
int blocked;
int delta;
signed int d_off = 0;
int on_beats;
long tot_on_beats;
long counts = 0;
long energy = 0;
volatile long secctr = 0;
volatile unsigned long pps_millis ;   // hold millis
unsigned long old_pps_millis;
bool chime_check = false;
bool volume_check = true;

int error;
unsigned long timeout;
unsigned int days = 0;

long arr_pps_millis[APM];

// time per minute (33 beats)
double tpm ;   // in milliseconds
unsigned long counts_millis;
double timepps;

char showtime = 'n';  // serial print the time every swing if y
char fs = 'n';        // fast slow not used
char vb = 'n';        // verbose mode if y
char white = 'y';     // LED to flash in sunc with the beat
char mode = 'a';      // auto control 'a' or none 'm'
int control;
int init_time = 0;

long movement = 0;     // beats since movement seen by PIR sensor

double center, center2;

// a string to hold incoming data
String inputString;
//inputString.reserve(20);
//inputString = "";

boolean stringComplete = false;  // whether the string is complete

boolean reset_flag = false;

byte gh, gm, gs;      // GPS counted time
byte ch, cm, xch;     // grandfather clock counted time
float cs;             // clock seconds

char rx_byte = 0;
String rx_str = "";

//**********************************************************
void setup() {
   
  char rx_byte = 0;
  //String rx_str = "";
  inputString.reserve(20);
  inputString = "";

  int a;

  pinMode(COIL, OUTPUT);
  pinMode(ledCenter, OUTPUT);
  pinMode(ledERR, OUTPUT);
  pinMode(ledBlink, OUTPUT);
  pinMode(SW, INPUT);
  pinMode(pps, INPUT);
  pinMode(resetMIDI, OUTPUT);
  pinMode(PIR, INPUT);

  digitalWrite(ledERR, HIGH);
  digitalWrite(ledBlink, LOW);
  control = 60;

  //Setup soft serial for MIDI control
  mySerial.begin(31250);   // software serial
  Serial.begin(115200);    // hardware serial

  // reset the MIDI controller
  resetVS1053();

//  inputString.reserve(20);

  for (int ii = 0; ii < ARRSIZE; ii++) {
    arrPeriod[ii] = 1818;
  }

  matrix.begin(0x70);   // I2C address of 70
  matrix.print(VERSION);
  boolean drawDots = false;
  matrix.drawColon(drawDots);
  matrix.writeDisplay();

  Serial.print ("\nversion ");
  Serial.println (VERSION);
  Serial.print ("Starting gdsPendulum\n");

/*
  Serial.println ("chiming");
  // 15 after
  for (int i = 0; i < 4; i++) {
    noteOn(0, chime[i], 60);
    delay(1000);
    if (i == 3 || i == 7 || i == 11) {
      delay(1000);
    }
    noteOff(0, chime[i], 60);
  }
*/
  Serial.print ("enter GPS clock time \n");

  while ( rx_byte != '\n') {
    if (Serial.available() > 0) {    // is a character available?
      rx_byte = Serial.read();       // get the character

      if (rx_byte != '\n') {
        // a character of the string was received
        rx_str += rx_byte;
      }
      else {
        // end of string
        Serial.println(rx_str);
      }
    }
  }

  long myTime = rx_str.substring(0, 6).toInt();

  Serial.print("myTime ");
  Serial.println(myTime);
  gh = myTime / 10000;
  myTime = myTime % 10000;
  gm = myTime / 100;
  gs = myTime % 100;

  attachInterrupt(digitalPinToInterrupt(pps), markpps, RISING);
  Serial.println(F("GPS attached"));
  Serial.println(F("Sync'ing pendulum to GPS"));

  for (int ii = 1; ii < 31; ii++) {

    // wait for pendulum to go on
    a = digitalRead(SW);
    while (a == 0) {
      a = digitalRead(SW);
    }

    // sensor just got blocked
    while (a == 1) {
      a = digitalRead(SW);
    }

    // just now unblocked
    ctr = millis();

    // pps_millis is time of GPS 1PPS pulse
    delta = ctr - pps_millis;
    Serial.println(delta);

//    if (delta < 100) {
    // 20 Nov 2017 - start with a little more offset
    // 20 Apr 2020 - widened delta to 450
    if ( delta > 250 && delta < 450 ) {
      init_time = ii;
      ii = 31;
    }

  }

  // -------------------------------

  // set clock time from GPS time
  ch = gh;
  cm = gm;
  cs = gs;
  secctr = 0;
  error = 0;

  Serial.println(F("Sync complete"));
  Serial.print(F("init_time = "));
  Serial.println(init_time);

  Serial.print(F("Starting times: "));
  printtime();
  Serial.println("");

} // end setup()



//**********************************************************
void loop() {

  if (stringComplete) {
    process_string();
  }
  checknote();

  // wait for sensor blocked
  timeout = millis(); // + 2000;
  a = digitalRead(SW);
  while (a == 0) {
    checknote();
    a = digitalRead(SW);
    // did pendulum miss the sensor (SW) ?
    if ((millis() - timeout) > 2000 ) {
      sound_alarm();
      matrix.blinkRate(1);
      matrix.writeDisplay();
      a = 1;
    }
  }

  // sensor just got blocked
  blocked = millis();

  cs = cs + 1.81818;
  if (cs > 59) {
    cs = 0;
    cm = cm + 1;
    if (cm > 59) {
      cm = 0;
      ch = ch + 1;
      if (ch > 23) {
        ch = 0;
        days = days + 1;
      }
    }
  }

// check newly installed PIR infrared sensor to keep chime volume down when
// movement is seen in the study 
// processed 33 times per minute (every 1.8 seconds)
  readPIR = digitalRead(PIR);
  if (readPIR == 1){
     movement = 0;

     // let's make sure the volume is always turned down if movement in the study
     if (volume != 70) {
       volume = 70;
       talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
     }
    
  }
  movement = movement + 1 ;

// 12 hour clock or 24 hour clock
  if (ch > 12) {
//    xch = ch - 12;
    xch = ch - 0;
  } else {
    xch = ch;
  }

  // display time as 12 hr
  if (showtime == 'n') {
    matrix.print(xch * 100 + cm);
  }else{
    matrix.print(gm * 100 + gs);
  }
  
  boolean drawDots = true;
  matrix.drawColon(drawDots);

  // automatic brightness control
  if (bmode == 'a') {
    if (ch >= 22 || ch < 6) {
      brightness = 1;
    } else if (ch >= 8) {
      brightness = 14;
    } else if (ch >= 6) {
      brightness = 11;
    }
  }

  matrix.setBrightness(brightness);
  matrix.writeDisplay();

  // adjust MIDI volume according to clock time.  Louder when no one is likely
  // to be in the study
  if (cm == 59) {
    volume_check = true;
  }
  // since we use the same times for volume as for brightness let's use the same auto/manual flag
  if (bmode == 'a' && volume_check == true) {
    // louder at nighttime unless someone is in the study
    if ((ch >= 22 || ch < 6) && cm == 1 && (movement > 2000) ) {
      volume = 120;
      talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
      Serial.print("Volume now= ");
      Serial.println(volume);
      volume_check = false;
    } else if (ch >= 6 && ch < 8 && cm == 1 && (movement > 2000) ) {
      volume = 80;
      talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
      Serial.print("Volume now= ");
      Serial.println(volume);
      volume_check = false;
    } else if (ch >= 8 && cm == 1 && (movement > 2000) ) {
      volume = 90;
      talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
      Serial.print("Volume now= ");
      Serial.println(volume);
      volume_check = false;
    } else if (cm == 1 && movement <= 2000) {
      volume = 70;
      talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
      Serial.print("Movement = ");
      Serial.print(movement);
      Serial.print("  Volume now = ");
      Serial.println(volume);
      volume_check = false;
   }
  }

// we reset the MIDI controller every day at 1:05 AM
  if (ch == 1 && cm == 4 ) {
    reset_flag = true;
  }

  if (cm == 5 && reset_flag == true){
    resetVS1053();
    reset_flag = false;
    Serial.print("\n MIDI controller reset \n");
  }
  
  if (gs > 59) {
    gs = gs - 60;
    gm = gm + 1;
    chime_check = true;
    if (gm > 59) {
      gm = 0;
      gh = gh + 1;
      if (gh > 23) {
        gh = 0;
      }
    }
  }

  if (stringComplete) {
    process_string();
  }

  if (cs == 0) {
    printtime();
    if (chime_check == true) {
      switch (cm) {
        case 15:
          play15();
          setptr = 0;
          setmillis = 0;
          break;
        case 30:
          play30();
          setptr = 0;
          setmillis = 0;
          break;
        case 45:
          play45();
          setptr = 0;
          setmillis = 0;
          break;
        case 0:
          play00();
          hourchime();
          setptr = 0;
          setmillis = 0;
          break;
        default:
          chime_check = false;
          break;
      }
      chime_check = false;
    }
  }

  if (showtime == 'y') {
    printtime();
    Serial.println("");
  }

  while (a == 1) {
    a = digitalRead(SW);
    checknote();
    if (stringComplete) {
      process_string();
    }

  }

  // sensor just got unblocked
  blocked = millis() - blocked;
  period = min(millis() - ctr, 1830);  // no big periods for us
  ctr = millis();
  counts = counts + 1;
  if (counts % 33 == 0) {
    tpm = millis() - counts_millis;
    counts_millis = millis();
  }
  center = ctr + period / 4 - blocked / 2;
  center2 = center + period / 2;

  avgPeriod = 0;
  for (int ii = 0; ii < ARRSIZE - 1; ii++) {
    arrPeriod[ii] = arrPeriod[ii + 1];
    avgPeriod = avgPeriod + arrPeriod[ii];
  }

  arrPeriod[ARRSIZE - 1] = period;
  avgPeriod = (avgPeriod + arrPeriod[ARRSIZE - 1]) / ARRSIZE;
  //  avgPeriod = avgPeriod * 1.0003151;

  if (on_beats > 0) {
    on_beats = on_beats - 1;
    digitalWrite(COIL, HIGH);   // set the COIL on
  } else {
    digitalWrite(COIL, LOW);   // set the COIL on
  }

  if (vb == 'y') {
    Serial.print("Period: ");
    Serial.print(period);
    Serial.print("  Avgp: ");
    Serial.print(avgPeriod, 3);
    Serial.print("  Blkd: ");
    Serial.print(blocked);
    Serial.print(" ");
    Serial.print(" energy: ");
    Serial.print(energy);
    Serial.print(" ");

    // delta = counts_millis - pps_millis;
    Serial.print("  delta: ");
    Serial.print(delta);
    Serial.print(" ");

    Serial.print("  delta offset: ");
    Serial.print(d_off);
    Serial.print(" ");

    Serial.print(" on_beats: ");
    Serial.print(on_beats);
    Serial.print(" ");

    Serial.print(" Counts: ");
    Serial.print(counts);
    Serial.print(" ");
  }


  // ***********************************
  // true once every minute
  if (counts % 33 == 0) {

    // C O N T R O L  -------------------------------
    // we can only speed up the pendulum
    // 20 Apr 2020 - changed error value to 2
    if (counts > 65  && mode == 'a' && error < 2) {
      if (delta > (500 + d_off)) {
        on_beats = min(int((delta - d_off - 500) / 4) + 16, 59);
        tot_on_beats = tot_on_beats + on_beats;
      } else if (delta > (400 + d_off)) {
        on_beats = int((delta - d_off - 400) / 9) + 6;
        tot_on_beats = tot_on_beats + on_beats;
      } else if (delta > (300 + d_off)) {
        on_beats = int((delta - d_off - 300) / 18) + 1;
        tot_on_beats = tot_on_beats + on_beats;
      } else {
        on_beats = 0;
      }
    }

    if (vb == 'y') {
      Serial.println("");
    }

    // print clock time and more
    Serial.print("Day ");
    Serial.print(days);
    Serial.print(" ");
    printDigits(ch);
    Serial.print(":");
    printDigits(cm);
    Serial.print(":");
    printDigits(cs);
    Serial.print(" ");

 if (vb == 'n') {
    Serial.print(" mode:");
    Serial.print(mode);
    Serial.print(" Counts: ");
    Serial.print(counts);
    Serial.print(" Secs: ");
    Serial.print(secctr);
    Serial.print(" mod ");
    Serial.print(((secctr) % 60));
 }
    error = (60 * (counts / 33)) - (secctr);

    if (error == 0) {
      digitalWrite(ledERR, LOW);
      matrix.blinkRate(0);
      matrix.writeDisplay();
    } else {
      digitalWrite(ledERR, HIGH);
    }

 if (vb == 'n') {
    Serial.print(" err ");
    Serial.print(error);
    Serial.print(" ");
    Serial.print(" tpm: ");

    //      Serial.print(tpm*60000.0/avgppsmillis,1);

    Serial.print(tpm, 0);
    Serial.print(" ");
    int variance = int(tpm - timepps); // 59981);
    if (variance > -1) {
      Serial.print(" ");
    }
    if (abs(variance) < 10) {
      Serial.print(" ");
    }
    Serial.print(variance);
    Serial.print(" ppsmillis: ");
 }
    timepps = pps_millis - old_pps_millis;
    old_pps_millis =  pps_millis;
    if (vb == 'n') {
       Serial.print(timepps, 0);
    }
    long sumppsmillis = 0;
    float avgppsmillis = 0;
    for (int ii = 0; ii < APM - 1; ii++) {
      arr_pps_millis[ii] = arr_pps_millis[ii + 1];
      sumppsmillis =  sumppsmillis + arr_pps_millis[ii] ;
    }
    arr_pps_millis[APM - 1] = timepps;
    sumppsmillis =  sumppsmillis + timepps;

    avgppsmillis = float (sumppsmillis) / APM;
 
  if (vb == 'n') {
   Serial.print(" avg ");
    Serial.print(avgppsmillis, 1);

    // changed calculation to get more accurate average period
    Serial.print(" Avgp: ");
    Serial.print(avgPeriod, 2);
    //      Serial.print(avgPeriod*60000.0/avgppsmillis,1);

    Serial.print(" Blkd: ");
    Serial.print(blocked);

    Serial.print(" energy: ");
    Serial.print(energy);
  }
    // D E L T A  -------------------------------------
    delta = counts_millis - pps_millis;
  if (vb == 'n') {
    Serial.print(" delta: ");
    Serial.print(delta);

    Serial.print(" on_beats: ");
    Serial.print(on_beats);

    Serial.print(" movement: ");
    Serial.print(movement);
  }
    if (vb == 'n') {
      Serial.print("\n");
    }

  }  // end counts%33 == 0


  if (vb == 'y') {
    Serial.print("\n");
  }

  delay_val = period / 4 - control - blocked / 2 - 10 ;
  checknote();
  if (stringComplete) {
    process_string();
  }


  // adding code to re-energize the pendulum
  // E N E R G Y  -------------------------------------
  if (on_beats == 0 && blocked < 400) {
    delay(delay_val);
    digitalWrite(COIL, HIGH);   // set the COIL on
    delay(control);                 // wait a bit
    digitalWrite(COIL, LOW);    // set the COIL off
    energy += 1;
  }

  checknote();

  // wait until 2 miliseconds before center
  while (millis() < center - 2) {
    checknote();
  }

  if (white == 'y') {
    digitalWrite(ledCenter, 1);
  }
  delay(4);
  digitalWrite(ledCenter, 0);

  if (stringComplete) {
    process_string();
  }

  // now should be at center + 4 msec

  // get ready to blink white LED on return swing
  while (millis() < center2 - 4) {
    checknote();
    if (stringComplete) {
      process_string();
    }
  }
  if (white == 'y') {
    digitalWrite(ledCenter, 1);
  }
  delay(8);
  digitalWrite(ledCenter, 0);

  // now past center on swing right
  checknote();
  if (stringComplete) {
    process_string();
  }
}     // end loop


//**********************************************************
void process_string() {
  //   Serial.println(inputString.substring(0,1));
  if (inputString.substring(0, 1) == "v") {
    // is a volume command
    if (inputString.length() > 2) {
      int volume = inputString.substring(1, 4).toInt();
      volume = min(volume, 127);
      volume = max(volume, 30);
      Serial.print("\nVolume now= ");
      Serial.println(volume);
      talkMIDI(0xB0, 0x07, volume);     //0xB0 is channel message, 0x07 is set channel volume
      inputString = "";
      stringComplete = false;
    } else {
      // is a verbose command
      Serial.print("verbose is now = ");
      if (vb == 'y') {
        vb = 'n';
      } else {
        vb = 'y';
      }
      Serial.print(vb);
      Serial.print("\n");
      inputString = "";
      stringComplete = false;
    }
  }
  else if (inputString.substring(0, 1) == "w") {
    Serial.print("white is now = ");
    if (white == 'y') {
      white = 'n';
    } else {
      white = 'y';
    }
    Serial.print(white);
    Serial.print("\n");
    inputString = "";
    stringComplete = false;
  }
  else if (inputString.substring(0, 1) == "m") {
    Serial.print("mode is now = ");
    mode = 'm';
    Serial.print(mode);
    Serial.print("\n");
    inputString = "";
    stringComplete = false;
  }
  else if (inputString.substring(0, 1) == "a") {
    Serial.print("mode is now = ");
    mode = 'a';
    Serial.print(mode);
    Serial.print("\n");
    inputString = "";
    stringComplete = false;
  }
  else if (inputString.substring(0, 1) == "d") {
    if (inputString.length() > 3) {
      if (inputString.substring(1, 1) == "-") {
         d_off = inputString.substring(2, 7).toInt();
         d_off = d_off * -1;
      }else{
         d_off = inputString.substring(1, 7).toInt();
      }
      Serial.print("delta offset ");
      Serial.println(d_off);
      inputString = "";
      stringComplete = false;
    }
  }
  else if (inputString.substring(0, 1) == "t") {
    //Serial.println(inputString.length());
    if (inputString.length() > 3) {
      long myTime = inputString.substring(1, 7).toInt();
      Serial.print("myTime ");
      Serial.println(myTime);
      gh = myTime / 10000;
      myTime = myTime % 10000;
      gm = myTime / 100;
      gs = myTime % 100;
      ch = gh;
      cm = gm;
      cs = gs;
      counts = counts - (counts % 33);
      secctr = int(60 * counts / 33) + 1;
    }
    else {
      Serial.print("showtime is now = ");
      if (showtime == 'y') {
        showtime = 'n';
      } else {
        showtime = 'y';
      }
      Serial.println(showtime);
    }
    inputString = "";
    stringComplete = false;
  }
  else if (inputString.substring(0, 1) == "b") {
    if (inputString.substring(1, 2) == "m") {  // a or m
      bmode = 'm';
    } else {
      bmode = 'a';
    }
    if (bmode == 'm') {
      brightness = inputString.substring(2, 4).toInt();
    } else {
      Serial.print("on automatic brightness  ");
    }
    Serial.print("Bright ");
    Serial.println(brightness);
    inputString = "";
    stringComplete = false;
  }
  else if (inputString.substring(0, 1) == "c") {
    int chimecnt = inputString.substring(1, 2).toInt();
    switch (chimecnt) {
      case 1:
        play15();
        setptr = 0;
        setmillis = 0;
        break;
      case 2:
        play30();
        setptr = 0;
        setmillis = 0;
        break;
      case 3:
        play45();
        setptr = 0;
        setmillis = 0;
        break;
      case 0:
        play00();
        setptr = 0;
        setmillis = 0;
        break;
      default:
        // chime_check = false;
        break;
    }
    inputString = "";
    stringComplete = false;
  }
    else if (inputString.substring(0, 1) == "i") {
    instrument = inputString.substring(1, 5).toInt() ;
    Serial.print("\nInstrument now: ");
    Serial.println(instrument);
    instrument = min(instrument,127);
    talkMIDI(0xC0, instrument-1, 0);
//    resetVS1053();
    inputString = "";
    stringComplete = false;
  }
   else if (inputString.substring(0, 1) == "r") {
    Serial.print("reset midi ");
    resetVS1053();
    Serial.print("\n");
    inputString = "";
    stringComplete = false;
  }
  else {
    Serial.print("what?  ");
    inputString = "";
    stringComplete = false;
  }

}



//**********************************************************
void markpps() {

  // avoid signal bounce
  if (millis() - pps_millis  > 995) {
    pps_millis = millis();
    secctr = secctr + 1;
    gs = gs + 1;
  }
}


//**********************************************************
void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}

//**********************************************************
void printDigits(byte digits) {
  // utility function for digital clock display: prints leading 0
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

//**********************************************************
void printtime() {
  Serial.print("gfc:");
  printDigits(ch);
  Serial.print(":");
  printDigits(cm);
  Serial.print(":");
  printDigits(cs);
  Serial.print(" ");
  Serial.print("gps:");
  printDigits(gh);
  Serial.print(":");
  printDigits(gm);
  Serial.print(":");
  printDigits(gs);
  Serial.print("  ");
}


void resetVS1053() {
  //Reset the VS1053
  digitalWrite(resetMIDI, LOW);
  delay(100);
  digitalWrite(resetMIDI, HIGH);
  delay(100);
  talkMIDI(0xB0, 0x07, volume);  //0xB0 is channel message, 0x07 is set channel volume, max is 127
  talkMIDI(0xB0, 0, 0x00);       //Default bank GM1
  talkMIDI(0xC0, instrument-1, 0); //Set instrument number. 0xC0 is a 1 data byte command
}

//Send a MIDI note-on message.  Like pressing a piano key
//channel ranges from 0-15
void noteOn(byte channel, byte note, byte attack_velocity) {
  talkMIDI( (0x90 | channel), note, attack_velocity);
}

//Send a MIDI note-off message.  Like releasing a piano key
void noteOff(byte channel, byte note, byte release_velocity) {
  talkMIDI( (0x80 | channel), note, release_velocity);
}

//Plays a MIDI note. Doesn't check to see that cmd is greater than 127, or that data values are less than 127
void talkMIDI(byte cmd, byte data1, byte data2) {
  digitalWrite(ledBlink, HIGH);
  mySerial.write(cmd);
  mySerial.write(data1);

  //Some commands only have one data byte. All cmds less than 0xBn have 2 data bytes
  //(sort of: http://253.ccarh.org/handout/midiprotocol/)
  if ( (cmd & 0xF0) <= 0xB0)
    mySerial.write(data2);

  digitalWrite(ledBlink, LOW);
}

void play15() {
  // 15 after
  byte ptr = 0;
  for (int i = 0; i < 4; i++) {
    set[0][ptr] = chime[i];
    set[1][ptr] = 1000;
    if (i == 3 || i == 7 || i == 11) {
      set[1][ptr] = 2000;
    }
    ptr ++;
  }
  set[0][ptr] = 0;
}

void play30() {
  // 30 after
  byte ptr = 0;
  for (int i = 4; i < 12; i++) {
    set[0][ptr] = chime[i];
    set[1][ptr] = 1000;
    if (i == 7 || i == 11) {
      set[1][ptr] = 2000;
    }
    ptr ++;
  }
  set[0][ptr] = 0;
}

void play45() {
  // 45 after
  byte ptr = 0;
  for (int i = 12; i < 20; i++) {
    set[0][ptr] = chime[i];
    set[1][ptr] = 1000;
    if (i == 15 || i == 19 ) {
      set[1][ptr] = 2000;
    }
    ptr ++;
  }
  for (int i = 0; i < 4; i++) {
    set[0][ptr] = chime[i];
    set[1][ptr] = 1000;
    if (i == 3) {
      set[1][ptr] = 2000;
    }
    ptr ++;
  }
  set[0][ptr] = 0;
}
void play00() {
  // on the hour
  byte ptr = 0;
  for (int i = 4; i < 20; i++) {
    set[0][ptr] = chime[i];
    set[1][ptr] = 1000;
    if (i == 7 || i == 11 || i == 15 || i == 19) {
      set[1][ptr] = 2000;
    }
    ptr++;
  }
  set[0][ptr] = 0;
}


void hourchime() {
  // one per each hour
  byte ptr = 16;
  for (int i = 0; i < xch; i++) {
    set[0][ptr] = 52;
    set[1][ptr] = 2000;
    ptr ++;
  }
  set[1][ptr - 1] = 4000;
  set[0][ptr] = 0;
}

void checknote() {
  if (setptr < 99) {                      // we're playing a note
    if (setmillis == 0) {                 // starting the note
      setmillis = millis();
      noteOn(0, set[0][setptr], 60);
    }
    if (millis() - setmillis > set[1][setptr]) { // if the note time is up
      setmillis = 0;
      noteOff(0, set[0][setptr], 60);
      setptr ++;                               // next note
    }
    if (set[0][setptr] == 0) {       // no more notes
      setptr = 99;
    }
  }
}

void sound_alarm(){

      volume = 32;
      talkMIDI(0xB0, 0x07, volume);  // set a lower volume since we errored
      talkMIDI(0xC0, 125-1, 0);      // telephone ring
      int ptr = 0;
      Serial.println("Pendulum timeout!");
      for (int i = 0; i < 3; i++) {
        set[0][ptr] = 50;
        set[1][ptr] = 300;
        ptr ++;
        set[0][ptr] = 10;
        set[1][ptr] = 500;
        ptr ++;
      }
      set[0][ptr] = 10;
      set[1][ptr] = 10000;
      ptr ++;
      set[0][ptr] = 0;
      setptr = 0;
      setmillis = 0;
 
}
