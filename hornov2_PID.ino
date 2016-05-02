#include <LiquidCrystal.h>
#include <Keypad.h>
#include <PID_v1.h>
#include "Adafruit_MAX31855.h"
#include <Time.h>
#include <TimeAlarms.h>


//--------keypad setup
const byte ROWS = 4; 
const byte COLS = 4; 
// Define the Keymap
char keys[ROWS][COLS] = {
  {
    '1','2','3','A'  }
  ,
  {
    '4','5','6','B'  }
  ,
  {
    '7','8','9','C'  }
  ,
  {
    '*','0','#','D'  }
};
// Connect keypad ROW0, ROW1, ROW2 and ROW3 to these Arduino pins.
byte rowPins[ROWS] = { 
  10, 9, 8, 7 }; 
// Connect keypad COL0, COL1 and COL2 to these Arduino pins.
byte colPins[COLS] = { 
  6, 5, 4, 3 }; 

// Create the Keypad
Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

// LCD pins
LiquidCrystal lcd(A0, A1, A2, A3, A4, A5);

// Relay output pin
const int relayPin = 13;
boolean estadoHorno = false;

//Temp units constants
const int C = 1;
const int F = 2;

//Temp input pins
int thermoDO = 2;
int thermoCS = 11;
int thermoCLK = 12;
Adafruit_MAX31855 thermocouple(thermoCLK, thermoCS, thermoDO);

float temperature;

//Define Variables we'll be connecting to
double Setpoint, Input, Output;
//Specify the links and initial tuning parameters
PID myPID(&Input, &Output, &Setpoint,2,5,1, DIRECT);
int WindowSize = 2000;
unsigned long windowStartTime;

int stageNum = 98;
int stageTemp[99];
unsigned long stageTime[99];




/**********************************/
void setup()
{

  //se uso el pin 2 para conectar el lector de temp
  //buscar otra opcion para disparar la funcion de no hay luz
  //el interrupt para cuando se vaya la luz
  //el pin 2 debe estar con una pulldown (10k) a tierra y a los 5v externos.
  //attachInterrupt(0, noHayLuz, FALLING);

  Serial.begin(9600);

  //***PID
  windowStartTime = millis();
  //tell the PID to range between 0 and the full window size
  myPID.SetOutputLimits(0, WindowSize);
  //turn the PID on
  myPID.SetMode(AUTOMATIC);

  //LCD init (20chars, 4lines)
  lcd.begin(20, 4);

  //oven pins
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  ovenOff();


}

/************************************/
void loop()
{

  program();

  //one loop for each programed stage
  for (int stage = 0; stage < stageNum; stage++){ 
    unsigned long startPoint = millis();
    int startTemp = thermocouple.readFarenheit();
    lcd.clear();

    //stage clasification
    //0 - free time temp up
    //0 - tiempo libre sube temp
    //1 - free time temp down
    //1 - tiempo libre baja temp
    //2 - fixed time temp up
    //2 - tiempo fijo sube temp
    //3 - fixed time temp down
    //3 - tiempo fijo baja temp
    //4 - fixed time temp doesn't change
    //4 - tiempo fijo se mantiene temp
    int stageType = getTypeOf(stage);;

    //execute stage
    while (true){
      
      unsigned long millisVan = (millis() - startPoint);
      
      //temp management
      //if we need to correct temp
      if((stageType == 2) || (stageType == 3)){
        //temperatura a subir en la etapa
        int deltaTemp = (stageTemp[stage] - startTemp);
        //periodo de tiempo que debe tardar en subir cada grado
        int pasito = (stageTime[stage] / deltaTemp);
        //llama a ir a la temperatura de acuardo al tiempo que ha pasado
        tempGoTo((millisVan / pasito) + startTemp);
        
      } 
      //si no hay que ir corrigiendo
      else {tempGoTo(stageTemp[stage]);}
      
      //si la cancela el usuario
      if(cancelInput()){break;}

      //ESTA PARTE SOLO REFRESCA PANTALLA Y SALE SI SE ACABO EL TIEMPO
      //si se dio un tiempo fijo para la etapa
      if (stageTime[stage] > 0) {
        //si se acabo el tiempo break
        if(millisVan >= stageTime[stage]){break;}
        //cuanto tiempo falta
        unsigned long millisFaltan = stageTime[stage] - millisVan;
        refresh(stage, millisFaltan, stageTemp[stage]);
      } 
      //si se dio cero en el tiempo asumimos que queremos alcanzar la temp lo antes posible
      else {
        if(abs(thermocouple.readFarenheit() - stageTemp[stage]) < 2){break;}
        refresh(stage, 0, Setpoint);
      }
    }
    
    //apago el horno al final de cada etapa para asegurar que no se quede prendido en ningun caso
    //si la siguiente etapa necesita calentar, lo prende ella
    ovenOff();

  }

  fin();

}

//TODO*******************************************
//**YA**subdivision de etapas para subir cada grado en determinado tiempo
//**YA**si voy a subir 60 grados en 1 minuto toca a 1 segundo por grado (subdividirlo por grado)
//que pasa cuando no da tiempo de bajar la temperatura en el periodo de tiempo dado.
//poner chequeo de error que no te deje bajar temperatura en un tiempo dado, solo en tiempo 0.
//ponerle proteccion de errores en los inputs (solo numeros y mayores de cero)

//--------------------------------------------------
//funcion para clasificar la etapa
//0 - tiempo libre sube temp
//1 - tiempo libre baja temp
//2 - tiempo fijo sube temp
//3 - tiempo fijo baja temp
//4 - tiempo fijo se mantiene temp
int getTypeOf(int eta){
  
  int stageType;
  //tiempo libre
  if (stageTime[eta] == 0) {
    //si es la primera etapa, asumimos que sube
    if (eta == 0){
      stageType = 0;
    }
    //si la etapa anterior tiene temp menor, sube
    else if (stageTemp[eta - 1] < stageTemp[eta]){
      stageType = 0;
    }
    //si no, baja
    else {
      stageType = 1;
    }
  }
  //tiempo fijo
  else {
    //si es la primera etapa, asumimos que sube
    if (eta == 0){
      stageType = 2;
    }
    //si la etapa anterior tiene temp menor, sube
    else if (stageTemp[eta - 1] < stageTemp[eta]){
      stageType = 2;
    }
    //si es igual, se mantiene
    else if (stageTemp[eta - 1] == stageTemp[eta]){
      stageType = 4;
    }
    //si no, baja
    else {
      stageType = 3;
    }
  }

return (stageType);

}



//--------------------------------------------------
//funcion para programar el horno
void program(){
  int i = 0;
  while(i < stageNum){

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Paso " );
    lcd.print(i+1);
    stageTemp[i] = tempInput(i);
       
    if (stageTemp[i] > 0){
      stageTime[i] = timeInput(i);
    }

    i++;
  }

}


//--------------------------------------------------
//temp input
int tempInput(int et){
  int columna = 10;
  String  stringTemp = "";
  lcd.setCursor(0, 1);
  lcd.print("(F) temp? " );
  lcd.setCursor(0, 4);
  lcd.print("# enter     * go" );
  while(1){
    char key = kpd.getKey();
    lcd.setCursor(columna, 1);
    if (key != NO_KEY){
      if (key == '*'){
        stageNum = et;
        return(-1);
      }
      else if (key == '#'){
        int valor = stringTemp.toInt();
        return(valor); 
      }
      stringTemp = stringTemp + key;
      lcd.print(key);
      columna++;  
    }

  } 


}
//--------------------------------------------------
//time input
unsigned long timeInput(int et){
  int columna = 12;
  String  stringTime = "";
  lcd.setCursor(0, 2);
  lcd.print("(min) time? " );
  lcd.setCursor(0, 4);
  lcd.print("# enter         " );
  while(1){
    char key = kpd.getKey();
    lcd.setCursor(columna, 2);
    if (key != NO_KEY){
      if (key == '#'){
        int valor = stringTime.toInt();
        return(valor*60000); 
      }
      stringTime = stringTime + key;
      lcd.print(key);
      columna++;  
    }

  } 


}

//--------------------------------------------------
//cancel input
boolean cancelInput(){
  char key = kpd.getKey();
  if (key != NO_KEY){
    if (key == '*'){
      return(true); 
    }
    return(false);
  }  

}

//--------------------------------------------------
//fin
void fin(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LISTO" );
  
  //a ver si con esto se arregla el maximo de temp al terminar
  ovenOff();
  
  
  /*lcd.setCursor(0, 2);
   lcd.print("Para continuar" );
   lcd.setCursor(0, 3);
   lcd.print("presiona #" );*/

  while(true){
    //    poner un mensaje de pica * para reiniciar
    delay(500);
    char key = kpd.getKey();
    if (key != NO_KEY){
      if (key == '#'){ 

        break; 
      }  
    }
  }
} 
//--------------------------------------------------
//arrancar el PID con un setpoint en Farenheit
//PID
void tempGoTo(double goTo){
  Setpoint = goTo;
  Input = thermocouple.readFarenheit();
  myPID.Compute();
  /************************************************
   * turn the output pin on/off based on pid output
   ************************************************/
  if(millis() - windowStartTime>WindowSize)
  { //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  if(Output < millis() - windowStartTime) {
    ovenOff();    
  } 
  else {
    prenderHorno();
  }
}

//--------------------------------------------------
//acciones a tomar si se fue la luz
void noHayLuz(){
  //la accion puede ser reportar en que etapa y hora-minuto se quedo.
  ovenOff();
  lcd.clear();
  lcd.print("Se fue la luz!!!");
}

//---------------------------------------------------
//funcion para refrescar el LCD
void refresh(int current, unsigned long resta, int spEtapa){

  lcd.setCursor(0, 0);
  lcd.print("Paso " );
  lcd.print(current+1);
  lcd.print("/");
  lcd.print(stageNum);
  lcd.print("  (" );
  int horas = resta/3600000 ;
  if (horas < 10){lcd.print("0");}
  lcd.print(horas);
  lcd.print(":" );
  int minutos = (resta%3600000)/60000 ;
  if (minutos < 10){lcd.print("0");}
  lcd.print(minutos);
  lcd.print(":" );
  int segundos = ((resta%3600000)%60000)/1000;
  if (segundos < 10){lcd.print("0");}
  lcd.print(segundos);
  lcd.print(")" );

  lcd.setCursor(0, 1);
  lcd.print(thermocouple.readFarenheit(), 1);
  lcd.print(" F - ");
  lcd.print(thermocouple.readCelsius(), 1);
  lcd.print(" C  ");

  lcd.setCursor(0, 2);
  lcd.print("=> ");
  lcd.print(Setpoint, 0);
  lcd.print(" F ");
  lcd.setCursor(18, 2);
  if(estadoHorno){
    lcd.print("on");
  }
  else{
    lcd.print("  ");
  }
  lcd.setCursor(0, 4);
  //lcd.print("* para cancelar");
  lcd.print("=> ");
  lcd.print(spEtapa);
  lcd.print(" F ");
}

//---------------------------------------------------
//funcion para prender el horno
void prenderHorno(){
  digitalWrite(relayPin, HIGH);
  estadoHorno = true;
}

//---------------------------------------------------
//funcion para apagar el horno
void ovenOff(){
  digitalWrite(relayPin, LOW);
  estadoHorno = false;
}


//----------------------------------------------------
//funcion para convertir centigrados a farenheit
double c2f(double tGrados){
  return(((9/5) * tGrados) + 32);
}
//----------------------------------------------------
//funcion para convertir farenheit a centigrados
double f2c(double tGrados){
  return((5/9) * (tGrados - 32));
}




