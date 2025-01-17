
/* 
 Sharespresso by c't/Peter Siering
 
 is an Arduino-based RFID payment system for coffeemakers with toptronic logic unit, as Jura 
 Impressa S95 and others without modifying the coffeemaker itself. 

 Based on Oliver Krohns famous Coffeemaker-Payment-System 
 at https://github.com/oliverk71/Coffeemaker-Payment-System
 
 Hardware used: Arduino Uno, 16x2 LCD I2C, pn532/mfrc522 rfid card reader (13.56MHz), 
 HC-05 bluetooth, male/female jumper wires (optional: ethernet shield, buzzer, button)
 
 The code is provided 'as is', without any guarantuee. Use at your own risk! 

 Version 1.0
 (Eisbaeeer)
 20191002 - Anzeige abgebucht alter Betrag --> neuer Betrag
*/

// needed for conditional includes to work, don't ask why ;-)
char trivialfix;

// options to include into project
#define BUZZER 1 // piezo buzzer
#define BUZPIN 5  // digital pin for buzzer
//#define SERVICEBUT 8 // button to switch to service mode 
#define BT 1 // bluetooth module
#define LCD 1 // i2c lcd
#define SERLOG 1 // logging to serial port
#define DEBUG 1 // some more logging
//#define MEMDEBUG 1 // print memory usage 
//#define RFID 1 // stop on missing rfid reader
//#define NET 1 // include networking
//#define SYSLOG 1 // log to a log host
//#define USE_PN532 1 // pn532 as rfid reader
#define USE_MFRC522 1 // mfrc522 as rfid reader

// set your application specific settings here
#define MASTERCARD 2441853729 // card uid to enter/exit service mode
// coffemaker model
#define X7 // x7/saphira
//#define S95 1
// network configuration
#if defined(NET)
byte my_mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0x60, 0xC5 }; // replace
byte my_ip[] = { 192, 168, 26, 6 };
byte my_gateway[] = { 192, 168, 26, 251 };
byte my_dns[] = { 192, 168, 26, 5 };
byte my_subnet[] = { 255, 255, 255, 0 }; // if unusal has to be stet
#if defined(SYSLOG)
byte my_loghost[] = { 192, 168, 26, 254 };
char my_fac[] = "sharespresso";
String empty="";
#endif
#endif

// include selected libraries
#include <Wire.h>
#include <SoftwareSerial.h>
#if LCD > 0
#include <LiquidCrystal_I2C.h>
#endif
#include <EEPROMex.h>
#include <SPI.h>
#if USE_PN532 > 0
#include <PN532_SPI.h> // https://github.com/Seeed-Studio/PN532
#include <PN532.h>
#endif
#if USE_MFRC522 > 0
#include <MFRC522.h> // https://github.com/miguelbalboa/rfid.git
#endif
#if NET > 0
#include <Ethernet.h>
#if SYSLOG > 0
#include <Syslog.h> // https://github.com/tomoconnor/ardusyslog/
#endif
#endif

// hardware specific settings
#if defined(LCD)
LiquidCrystal_I2C lcd(0x27,16,2);
#endif
SoftwareSerial myCoffeemaker(2,3); // RX, TX
#if defined(BT)
SoftwareSerial myBT(7,6);
#endif
#if defined(USE_PN532)
#define PN532_SS 9 // select pin for PN532_SS
SPISettings nfc_settings(SPI_CLOCK_DIV8, LSBFIRST, SPI_MODE0);
PN532_SPI pn532spi(SPI, PN532_SS);
PN532 nfc(pn532spi);
#endif
#if defined(USE_MFRC522)
#define RST_PIN 8
#define SS_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);
#endif

// product codes send by coffeemakers "?PA<x>\r\n", just <x>
#if defined(S95)
char products[] = "EFABJIG";
#endif
#if defined(X7)
char products[] = "ABCHDEKJFG";
#endif

// general variables (used in loop)
boolean buttonPress = false;
const int n = 40; // number of cards, max is (1024-11*2)/6=167 on Arduino Uno
String BTstring=""; // contains what is received via bluetooth (from app or other bt client)
unsigned long time; // timer for RFID etc
unsigned long buttonTime; // timer for button press 
boolean override = false;  // to override payment system by the voice-control/button-press app
unsigned long RFIDcard = 0;
int inservice=0;
int price=0;
String last_product="";

// no more delay
unsigned long startMillis;  // some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long periode = 1000;  // one seconds
int displ_count;
int lcdwait;
bool lcdshow;

// LCD char Euro
byte customChar1[8] = {
                  B00110,
                  B01001,
                  B01000,
                  B11110,
                  B11110,
                  B01000,
                  B01001,
                  B00110
};


void setup()
{

/*--------------------------------------------------------------  
 * Milliseconds start
--------------------------------------------------------------*/
  startMillis = millis();  //initial start time

#if defined(SERLOG) || defined(DEBUG) || defined(MEMDEBUG)
  Serial.begin(9600);
#endif
#if defined(DEBUG)
  EEPROM.setMaxAllowedWrites(100);
  EEPROM.setMemPool(0, EEPROMSizeUno);
  Serial.println(sizeof(products));
#endif
#if defined(MEMDEBUG)
  Serial.println(free_ram());
#endif
#if defined(LCD)
  lcd.createChar(1, customChar1);
  lcd.init();
#endif
  message_print(F("sharespresso"), F("starting up"),2);
  myCoffeemaker.begin(9600);         // start serial communication at 9600bps
#if defined(BT)
  //myBT.begin(38400);
  myBT.begin(9600);
#endif
  // initialized rfid lib
#if defined(DEBUG)
  serlog(F("Initializing rfid reader"));
#endif
  SPI.begin();
#if defined(USE_PN532)  
  SPI.beginTransaction(nfc_settings);
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
#if defined(DEBUG)
    serlog(F("Didn't find PN53x board"));
#endif
#if defined(RFID)
    while (1); // halt
#endif
  }
#endif
#if defined(USE_MFRC522)
  mfrc522.PCD_Init(SS_PIN,RST_PIN);
  ShowReaderDetails();
#endif
  // configure board to read RFID tags and cards
#if defined(USE_PN532)
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0xfe);
  SPI.endTransaction();
#endif
  // configure service button
#if defined(SERVICEBUT)
  pinMode(SERVICEBUT,INPUT);
#endif
#if defined(NET)
  // disable card reader, bothers ethernet sometimes
  pinMode( 4, OUTPUT);
  digitalWrite( 4, HIGH);
  delay( 1);
  serlog( F("Starting network ..."));
  Ethernet.begin(my_mac, my_ip, my_dns, my_gateway, my_subnet);
#if defined(SYSLOG)
  serlog( F("Start logging ..."));
  Syslog.setLoghost(my_loghost);
  Syslog.logger(1,5,my_fac,empty, "start");
#endif
#endif
  message_print(F("Ready to brew"), F(""),5);
#if defined(MEMDEBUG)
  Serial.println(free_ram());
#endif
  // activate coffemaker connection and inkasso mode
  myCoffeemaker.listen();
  inkasso_on();
}

void loop()
{

/*--------------------------------------------------------------  
 * remove delay (one second)
--------------------------------------------------------------*/
  currentMillis = millis();  //get the current "time" (actually the number of milliseconds since the program started)
  if (currentMillis - startMillis >= periode)  // Hier eine Sekunde (periode)
  { 
  startMillis = currentMillis;
  displ_count++;
  Serial.print("Display count: ");
  Serial.println(displ_count);
  }

/*---------------------------------------------------------------
 * end one second
 ----------------------------------------------------------------*/
  
#if defined(MEMDEBUG)
  Serial.println(free_ram());
#endif
#if defined(SERVICEBUT)
  if ( digitalRead(SERVICEBUT) == HIGH) {
    servicetoggle();
  }
#endif  
  // Check if there is a bluetooth connection and command
  BTstring = "";
  // handle serial and bluetooth input
#if defined(BT)
    while( myBT.available()) {
      BTstring +=String(char(myBT.read()));
      delay(7);
    }
#endif
  while( Serial.available() ){  
    BTstring += String(char(Serial.read()));
    delay(7);  
  }
  BTstring.trim();
  
  if (BTstring.length() > 0){
    // BT: Start registering new cards until 10 s no valid, unregistered card
#if defined(DEBUG)
    serlog(BTstring);
#endif
#if defined(SYSLOG)
    Syslog.logger(1,5,my_fac,empty,"cmd "+ BTstring);    
#endif
    if( BTstring == "RRR" ){          
      time = millis();
      beep(1);
      message_print(F("Registering"),F("new cards"),5);
      registernewcards();
    }
    // BT: Send RFID card numbers to app    
    if(BTstring == "LLL"){  // 'L' for 'list' sends RFID card numbers to app   
      for(int i=0;i<n;i++){
#if defined(BT)
        unsigned long card=EEPROM.readLong(i*6);
        myBT.print(print10digits(card)); 
        if (i < (n-1)) myBT.write(',');  // write comma after card number if not last
#endif
      }
    }
    // BT: Delete a card and referring credit   
    if(BTstring.startsWith("DDD") == true){
      BTstring.remove(0,3); // removes "DDD" and leaves the index
      int i = BTstring.toInt();
      i--; // list picker index (app) starts at 1, while RFIDcards array starts at 0
      unsigned long card= EEPROM.readLong(i*6);
      int credit= EEPROM.readInt(i*6+4);      
      message_print(print10digits(card), F("deleting"), 5);
#if defined(SYSLOG)
      Syslog.logger(1,5,my_fac,empty,"delete "+ print10digits(card)+ " "+ printCredit(credit));
#endif      
      EEPROM.updateLong(i*6, 0);
      EEPROM.updateInt(i*6+2, 0);
      beep(1);
    }    
    // BT: Charge a card    
    if((BTstring.startsWith("CCC") == true) ){  // && (BTstring.length() >= 7 )
      char a1 = BTstring.charAt(3);  // 3 and 4 => card list picker index (from app)
      char a2 = BTstring.charAt(4);
      char a3 = BTstring.charAt(5);  // 5 and 6 => value to charge
      char a4 = BTstring.charAt(6);    
      BTstring = String(a1)+String(a2); 
      int i = BTstring.toInt();    // index of card
      BTstring = String(a3)+String(a4);
      int j = BTstring.toInt();   // value to charge
      j *= 100;
      i--; // list picker index (app) starts at 1, while RFIDcards array starts at 0  
      int credit= EEPROM.readInt(i*6+4);
      credit+= j;
      EEPROM.writeInt(i*6+4, credit);
      beep(1);
      unsigned long card=EEPROM.readLong(i*6);
      message_print(print10digits(card),"+"+printCredit(j), 5);
#if defined(SYSLOG)
      Syslog.logger(1,5,my_fac,empty,"charge "+ print10digits(card)+ " "+ printCredit(j));
#endif      
    }
    // BT: Receives (updated) price list from app.  
    if(BTstring.startsWith("CHA") == true){
      int k = 3;
      for (int i = 0; i < 11;i++){  
        String tempString = "";
        do {
          tempString += BTstring.charAt(k);
          k++;
        } 
        while (BTstring.charAt(k) != ','); 
        int j = tempString.toInt();
        Serial.println(i*2+1000);
        EEPROM.updateInt(i*2+1000, j);
        k++;
      }
      beep(1);
      message_print(F("Pricelist"), F("updated!"), 5);
    }
    // BT: Sends price list to app. Product 1 to 10 (0-9), prices divided by commas plus standard value for new cards
    if(BTstring.startsWith("REA") == true){   
      for (int i = 0; i < 11; i++) {
#if defined(BT)
        price= EEPROM.readInt(1000+i*2);
        myBT.print(int(price/100));
        myBT.print('.');
        if ((price%100) < 10){
          myBT.print('0');
        }
        myBT.print(price%100);
        if (i < 10) myBT.write(',');
#endif
      }
    } 

    if(BTstring == "?M3"){
      inkasso_on();
    }
    if(BTstring == "?M1"){
      inkasso_off();  
    }
    if(BTstring == "FA:04"){        // small cup ordered via app
      toCoffeemaker("FA:04\r\n"); 
      override = true;
    }
    if(BTstring == "FA:06"){        // large cup ordered via app
      toCoffeemaker("FA:06\r\n");  
      override = true;
    }
    if(BTstring == "FA:0C"){        // extra large cup ordered via app
      toCoffeemaker("FA:0C\r\n");  
      override = true;
    }    
  }          

  // Get key pressed on coffeemaker
  String message = fromCoffeemaker();   // gets answers from coffeemaker 
  if (message.length() > 0){
    serlog( message);
#if defined(SYSLOG)
    Syslog.logger(1,5,my_fac,empty,"coffeemaker "+ message);
#endif
    if (message.charAt(0) == '?' && message.charAt(1) == 'P'){     // message starts with '?P' ?
      buttonPress = true;
      buttonTime = millis();
      int product = 255;
      for (int i = 0; i < sizeof(products); i++) {
        if (message.charAt(3) == products[i]) {
          product = i;
          break;
        }
      }
      if ( product != 255) {
        String productname;
          switch (product) {
#if defined(S95)
            case 0: productname = F("Small cup"); break;
            case 1: productname = F("2 small cups"); break;
            case 2: productname = F("Large cup"); break;
            case 3: productname = F("2 large cups"); break;
            case 4: productname = F("Steam 2"); break;
            case 5: productname = F("Steam 1"); break;
            case 6: productname = F("Extra large cup"); break;
#endif
#if defined(X7)
            case 0: productname = F("Cappuccino"); break;
            case 1: productname = F("Espresso"); break;
            case 2: productname = F("Milchkaffee"); break;
            case 3: productname = F("Milchkaffee"); break;
            case 4: productname = F("Kaffee"); break;
            case 5: productname = F("Kaffee gross"); break;
            case 6: productname = F("Heisswasser"); break;
            case 7: productname = F("Dampf"); break;
            case 8: productname = F("Kanne Kaffee"); break;
            case 9: productname = F("Milchschaum"); break;
#endif
          }
        price = EEPROM.readInt(product* 2+ 1000);
        last_product= String(message.charAt( 3))+ "/"+ String(product)+ " ";
        message_print(productname, printCredit(price), 5);
      } 
      else {
        message_print(F("Error unknown"), F("product"), 5);
        buttonPress = false;
      }
      // boss mode, he does not pay
      if (override == true){
        price = 0;
      }
    }
  }
  // User has five seconds to pay
  if (buttonPress == true) {
    if (millis()-buttonTime > 5000){  
      buttonPress = false;
      price = 0;
      last_product = "";
      message_clear();
    }
  }
  if (buttonPress == true && override == true){
    toCoffeemaker("?ok\r\n");
    buttonPress == false;
    override == false;
  }
  // RFID Identification      
  RFIDcard = 0;  
  time = millis(); 
  do {
    RFIDcard = nfcidread();
    if (RFIDcard == MASTERCARD) {
      servicetoggle();
      delay(60);
      RFIDcard= 0;
    }
    if (RFIDcard != 0) {
#if defined(LCD)
      lcd.clear();
#endif
      break; 
    }           
  } 
  while ( (millis()-time) < 60 );  

  if (RFIDcard != 0){
    int k = n;
    for(int i=0;i<n;i++){         
      if (((RFIDcard) == (EEPROM.readLong(i*6))) && (RFIDcard != 0 )){
        k = i;
        int credit= EEPROM.readInt(k*6+4);
        if(buttonPress == true){                 // button pressed on coffeemaker?
           if ((credit - price) > 0) {
            int newCredit = (credit - price); // calculate new credit
            message_print(print10digits(RFIDcard), printCredit(credit) + " --> " + printCredit(newCredit), 5);
            EEPROM.writeInt(k*6+4, ( credit- price));   // write new credit to eeprom           
            toCoffeemaker("?ok\r\n");            // prepare coffee
#if defined(SYSLOG)
            Syslog.logger(1,5,my_fac,empty,"sell "+ print10digits(RFIDcard)+" "+ last_product+ printCredit( price));
#endif
            buttonPress= false;
            price= 0;
            last_product= "";
          } 
          else {
            beep(2);
            message_print(printCredit(credit), F("Not enough"), 5); 
          }
        } 
        else {                                // if no button was pressed on coffeemaker / check credit
          message_print(printCredit(credit), F("Remaining credit"), 5);
#if defined(SYSLOG)
          Syslog.logger(1,5,my_fac,empty,"credit "+print10digits(RFIDcard)+" "+printCredit(credit));
#endif
        }
        i = n;      // leave loop (after card has been identified)
      }      
    }
    if (k == n){ 
      k=0; 
      beep(2);
      message_print(String(print10digits(RFIDcard)),F("card unknown!"),5);
#if defined(SYSLOG)
      Syslog.logger(1,5,my_fac,empty,"unknown "+print10digits(RFIDcard));
#endif
    }     	    
  delay(1000);
  }

if (lcdshow == 1) {
    if (displ_count >= lcdwait) {
    // delay(wait);
    displ_count = 0;
    lcdshow = 0;
    lcd.clear();
    lcd.noBacklight();
    }
}

}
/*--------------------------------------
 * SUB
 */

String fromCoffeemaker(){
  String inputString = "";
  byte d0, d1, d2, d3;
  char d4 = 255;
  while (myCoffeemaker.available()){    // if data is available to read
    d0 = myCoffeemaker.read();
    delay (1); 
    d1 = myCoffeemaker.read();
    delay (1); 
    d2 = myCoffeemaker.read();
    delay (1); 
    d3 = myCoffeemaker.read();
    delay (7);
    bitWrite(d4, 0, bitRead(d0,2));
    bitWrite(d4, 1, bitRead(d0,5));
    bitWrite(d4, 2, bitRead(d1,2));
    bitWrite(d4, 3, bitRead(d1,5));
    bitWrite(d4, 4, bitRead(d2,2));
    bitWrite(d4, 5, bitRead(d2,5));
    bitWrite(d4, 6, bitRead(d3,2));
    bitWrite(d4, 7, bitRead(d3,5));
    inputString += d4;
  }
  inputString.trim();
  if ( inputString != "") {
        return(inputString);
  } 
}

void toCoffeemaker(String outputString)
{
  for (byte a = 0; a < outputString.length(); a++){
    byte d0 = 255;
    byte d1 = 255;
    byte d2 = 255;
    byte d3 = 255;
    bitWrite(d0, 2, bitRead(outputString.charAt(a),0));
    bitWrite(d0, 5, bitRead(outputString.charAt(a),1));
    bitWrite(d1, 2, bitRead(outputString.charAt(a),2));  
    bitWrite(d1, 5, bitRead(outputString.charAt(a),3));
    bitWrite(d2, 2, bitRead(outputString.charAt(a),4));
    bitWrite(d2, 5, bitRead(outputString.charAt(a),5));
    bitWrite(d3, 2, bitRead(outputString.charAt(a),6));  
    bitWrite(d3, 5, bitRead(outputString.charAt(a),7)); 
    myCoffeemaker.write(d0); 
    delay(1);
    myCoffeemaker.write(d1); 
    delay(1);
    myCoffeemaker.write(d2); 
    delay(1);
    myCoffeemaker.write(d3); 
    delay(7);
  }
}

String printCredit(int credit){
  int euro = ((credit)/100);  //  int euro = ((credit*10)/100);
  int cent = ((credit)%100);  //  int cent = ((credit*10)%100); 
  String(output);
  output = String(euro);
  output += ',';
  output += String(cent);
  if (cent < 10){
    output += '0';
  }
  //output += F(" EUR");
  //output += "\1"; 
  return output;
}

String print10digits(unsigned long number) {
  String(tempString) = String(number);
  String(newString) = "";
  int i = 10-tempString.length();
  for (int a = 0; a < (10-tempString.length()); a++){
    newString += "0";
  }
  newString += number;
  return newString;
}

String print2digits(int number) {
  String partString;
  if (number >= 0 && number < 10) {
    partString = "0";
    partString += number;
  } 
  else partString = String(number);
  return partString;
}

void serlog(String msg) {
#if defined(SERLOG)
  Serial.println(msg);
#endif
}

void message_print(String msg1, String msg2, int wait) {
  displ_count=0;
  lcdwait=0;
#if defined(SERLOG)
  if (msg1 != "") { Serial.print(msg1 + " "); }
  if (msg2 != "") { Serial.print(msg2); }
  if ((msg1 != "") || (msg2 != "")) { Serial.println(""); }
#endif
#if defined(LCD)
  lcd.clear();
  lcd.backlight();
  if (msg1 != "") {
    lcd.setCursor(0, 0);
    lcd.print(msg1);
  }
  if (msg2 != "") {
    lcd.setCursor(0, 1);
    lcd.print(msg2);
    //lcd.write(byte(1));
  }
    lcdwait = wait;   
    lcdshow = 1;
#endif
}

void message_clear() {
#if defined(LCD)
  lcd.clear();
  lcd.noBacklight();
#endif
}

void beep(byte number){
#if defined (BUZZER)
  int duration = 200;
  switch (number) {
  case 1: // positive feedback
    tone(BUZPIN,1500,duration);
    delay(duration);
    break;
  case 2: // negative feedback
    tone(BUZPIN,500,duration);
    delay(duration);
    break;     
  case 3:  // action stopped (e.g. registering) double beep
    tone(BUZPIN,1000,duration);
    delay(duration);
    tone(BUZPIN,1500,duration);
    delay(duration);    
    break; 
  case 4:  // alarm (for whatever)
    for (int a = 0; a < 3; a++){
      for (int i = 2300; i > 600; i-=50){
        tone(BUZPIN,i,20);
        delay(18);
      }     
      for (int i = 600; i < 2300; i+=50){
        tone(BUZPIN,i,20);
        delay(18);
      }
    }  
  }
#endif
}

void registernewcards() {
  do {
    RFIDcard = 0;
    do {
      RFIDcard = nfcidread();
      if (RFIDcard != 0) {
        message_clear();
        break;
      }
    } while ( (millis()-time) < 60 );  
    int k = 255;
    if (RFIDcard != 0) {
      if ( RFIDcard == MASTERCARD) {
        break;
      }
      for(int i=0;i<n;i++){
        if (RFIDcard == EEPROM.readLong(i*6)) {
          message_print(print10digits(RFIDcard), F("already exists"), 5);
          beep(2);
          k=254;         
          break;
        }
        if ((EEPROM.readLong(i*6) == 0) && (k == 255)) { // find first empty slot
          k=i;
        }
      }
      if ( k == 255) {
        message_print(F("no slot left"),F(""),5);         
        break;
      }
      if ( k != 254) {
        message_print( print10digits(RFIDcard), F("registered"),5);
        int credit= EEPROM.readInt(1000+2*10);
        EEPROM.updateLong(k*6, RFIDcard);
        EEPROM.updateInt(k*6+4, credit);
#if defined(SYSLOG)
        Syslog.logger(1,5,my_fac,empty,"load "+ print10digits(RFIDcard)+ " "+ printCredit(credit));
#endif
        beep(1);
      }
      time = millis();
    }
  } while ( (millis()-time) < 10000 );
  message_print(F("Registering"),F("ended"),5);
  beep(3);  
}

unsigned long nfcidread(void) {
  unsigned long id=0;
#if defined(USE_PN532)
  uint8_t success;
  uint8_t uid[] = { 0,0,0,0,0,0,0,0 };
  uint8_t uidLength;

  SPI.beginTransaction(nfc_settings);
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  SPI.endTransaction();
  
  if (success) {
    // ugly hack: fine for mifare classic (4 byte)
    // also fine for our ultras (last 4 bytes ever the same)
    // nfc.PrintHex(uid, uidLength);
    id = (unsigned long)uid[0]<<24;
    id += (unsigned long)uid[1]<<16;
    id += (unsigned long)uid[2]<<8;
    id += (unsigned long)uid[3];
  }
  return id;
#endif
#if defined(USE_MFRC522)
  if ( mfrc522.PICC_IsNewCardPresent()) {
#if defined(DEBUG)
    serlog(F("Found card"));
#endif
    if ( mfrc522.PICC_ReadCardSerial()) {
#if defined(DEBUG)
      serlog(F("Read id"));
#endif
      id = (unsigned long)mfrc522.uid.uidByte[0]<<24;
      id += (unsigned long)mfrc522.uid.uidByte[1]<<16;
      id += (unsigned long)mfrc522.uid.uidByte[2]<<8;
      id += (unsigned long)mfrc522.uid.uidByte[3];
      return id;
    }
  }
  return 0;
#endif
}

void servicetoggle(void){
    inservice=not(inservice);
    if ( inservice) {
      message_print(F("Service Mode"),F("started"),5);
#if defined(SYSLOG)
      Syslog.logger(1,5,my_fac,empty,"service on");
#endif
      inkasso_off();
#if defined(BT)
      myBT.listen();
#endif
    } else {
      message_print(F("Service Mode"),F("exited"),5);
#if defined(SYSLOG)
      Syslog.logger(1,5,my_fac,empty,"service off");
#endif
      myCoffeemaker.listen();
      inkasso_on();
    }
}

void inkasso_on(void){
  toCoffeemaker("?M3\r\n");  // activates incasso mode (= no coffee w/o "ok" from the payment system! May be inactivated by sending "?M3" without quotation marks)
  delay (100);               // wait for answer from coffeemaker
#if defined(LCD)
  lcd.backlight();
#endif
  if (fromCoffeemaker() == "?ok"){
    beep(1);
    message_print(F("Inkasso mode"),F("activated!"),5);  
    delay(2000);
  } else {
    beep(2);
    message_print(F("Coffeemaker"),F("not responding!"),5);  
  }  
}

void inkasso_off(void){
  toCoffeemaker("?M1\r\n");  // deactivates incasso mode (= no coffee w/o "ok" from the payment system! May be inactivated by sending "?M3" without quotation marks)
  delay (100);               // wait for answer from coffeemaker
  if (fromCoffeemaker() == "?ok"){
    beep(1);
    message_print(F("Inkasso mode"),F("deactivated!"),5);
    delay(2000);  
  } else {
    beep(2);
    message_print(F("Coffeemaker"),F("not responding!"),5);
  }
}

#if defined(MEMDEBUG)
int free_ram(void) { 
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
#endif

#if defined(USE_MFRC522)
void ShowReaderDetails() {
  // Get the MFRC522 software version
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("MFRC522 Software Version: 0x"));
  Serial.print(v, HEX);
  if (v == 0x91)
    Serial.print(F(" = v1.0"));
  else if (v == 0x92)
    Serial.print(F(" = v2.0"));
  else
    Serial.print(F(" (unknown)"));
  Serial.println("");
  // When 0x00 or 0xFF is returned, communication probably failed
  if ((v == 0x00) || (v == 0xFF)) {
    Serial.println(F("WARNING: Communication failure, is the MFRC522 properly connected?"));
  }
}
#endif
