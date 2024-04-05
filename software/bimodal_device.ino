
int BLUE_LED_PIN = 16;
int GREEN_LED_PIN = 17;
int AUDIO_RELAY_PIN = 1;
int TENS_RELAY_PIN = 4;
int TENS_BYPASS_PIN = 5;
int OPTO_PIN_1 = 2;
int OPTO_PIN_2 = 3;


unsigned long applicationStartTime                  =0;
volatile unsigned long risingPulseTimeMicros        =0;
volatile unsigned long fallingPulseTimeMicros       =0;
volatile unsigned long readTime_micros              =0;
volatile unsigned long lastRisingPulseTimeMicros    =0;
volatile unsigned long lastFallingPulseTime         =0;


volatile float period_micros         =0;
volatile float pulse_width_micros    =0;
volatile float frequency_Hz          =0;
volatile int nTotalPulses             =0;
volatile int timeToStartNextAudioMicros       =0;
volatile int audioStartTimeMicros             =0;
volatile int audioEndTimeMicros               =0;
volatile int predictedTimeOfNextPulseMicros       =0;
int averagePulsePeriodMicros =0;

int debounceMicros = 0;
int audioPulseWidthMicros = 10*1000;       //Number of microseconds audio should play for
int audioEndPreTENSDelayMicros  = 6*1000; //Number of microseconds prior to receiving electrical stim when audio should stop
int audioStartPreTENSMicros = audioPulseWidthMicros + audioEndPreTENSDelayMicros;  //Number of milliseconds prior to receiving electrical stim when audio should start

int bimodalFrequencyHz = 5;
float bimodalPeriodMillis = 1000/5;
int bimodalPeriodMicros = bimodalPeriodMillis*1000;
int numCyclesBetweenBimodals;
volatile int actualTimeBetweenBimodals;
int prevNTotalPulses;
volatile int numCyclesUntilNextbimodal;
bool setup_func = false;
int actualAudioOnTimeMicros;
int actualAudioOffTimeMicros;
int actualAudioPulseWidthMicros;
int TENSRElayOnTimeMicros;
int actualAudioTimeBeforeTENSPulse;
int actualTENSOnTime;
int actualTENSRelayTimeBeforePulse;
int TENSRElayOffTimeMicros;
int TENSRelayOnTime;


void setup() {
  applicationStartTime = micros();

  // LEDs
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  //relays
  pinMode(AUDIO_RELAY_PIN, OUTPUT);
  pinMode(TENS_RELAY_PIN, OUTPUT);
  pinMode(TENS_BYPASS_PIN, OUTPUT);

  digitalWrite(TENS_RELAY_PIN, HIGH);
  digitalWrite(AUDIO_RELAY_PIN, LOW);


  //optocouplers
  pinMode(OPTO_PIN_1, INPUT_PULLUP);
  pinMode(OPTO_PIN_2, INPUT_PULLUP);

  //Code setup
  attachInterrupt(digitalPinToInterrupt(OPTO_PIN_1), readOptoPin1, FALLING);
  // attachInterrupt(digitalPinToInterrupt(OPTO_PIN_2), readOptoPin2, CHANGE);

  Serial.begin(9600);
  // Serial.print("start\n");

}

void loop() {
  Serial.print("nTotalPulses:");
  Serial.print(nTotalPulses);
  Serial.print("\n");

  if (nTotalPulses >= 10 && setup_func == false) {    //Sets up the sync between the TENS unit and the RaspPi Pico
    averagePulsePeriodMicros = first1SecondSync();    //Runs the first sync
    numCyclesBetweenBimodals = (bimodalPeriodMicros / averagePulsePeriodMicros); //Calculates the required number of cycles to roughly match the desired bimodal schedule
    actualTimeBetweenBimodals = numCyclesBetweenBimodals * averagePulsePeriodMicros;  //Converts the number of cycles value to an actual time value that can be used to predict TENS pulses.
    Serial.print("bimodal Period Millis: ");
    Serial.print(bimodalPeriodMicros/1000);
    Serial.print("ms\nAveragePulsePeriodMicros: ");
    Serial.print(averagePulsePeriodMicros);
    Serial.print("us\nNum Cycles Between Bimodals: ");
    Serial.print(numCyclesBetweenBimodals);
    Serial.print("\nActual millis between bimodals: ");
    Serial.print(actualTimeBetweenBimodals/1000.00);
    Serial.print("ms\n");

    setup_func=true;
  }

  period_micros= (risingPulseTimeMicros - lastRisingPulseTimeMicros);   //pulse period in microseconds
  pulse_width_micros = fallingPulseTimeMicros - risingPulseTimeMicros;  //pulse width in microseconds
  frequency_Hz = 1.0000 / (period_micros/1000000);                      //frequency in Hz
  //Serial.print("Here");

  if (nTotalPulses > prevNTotalPulses && nTotalPulses >=10){                      //This freezes the program upon a bimodal pulse, and waits for the next anticipated TENS pulse
    prevNTotalPulses = nTotalPulses;                          //Resets the gate for whether this if statement is entered.  This will be updated later on after the TENS pulse is recorded by the ISR
    timeToStartNextAudioMicros = readTime_micros + actualTimeBetweenBimodals - audioStartPreTENSMicros;   //Calculates the proper time to start the next Audio segment.
    
    
    // Serial.print("Current Time: ");
    // Serial.print((micros())/1000.00);         
    // Serial.print("ms\n");
    
    // Serial.print("Read time: ");
    // Serial.print((readTime_micros)/1000.00);         
    // Serial.print("ms\n");

    // Serial.print("Inside bimodal function\nTime to start next audio in millis:");
    // Serial.print((timeToStartNextAudioMicros)/1000.00);    //Time to start next audio is calculating a time earlier than the current time.
    
    // Serial.print("ms\nLast Pulse time: ");
    // Serial.print((risingPulseTimeMicros)/1000.00);
    // Serial.print("ms\n");


    Serial.print("Delay Time: ");
    Serial.print((timeToStartNextAudioMicros-micros())/1000);
    Serial.print("ms\n");

    delayMicroseconds(timeToStartNextAudioMicros-micros());   //Delays until the proper time to start playing audio before the expected TENS pulse

    digitalWrite(AUDIO_RELAY_PIN, HIGH);                      //Turns on the audio
    actualAudioOnTimeMicros = micros();

    delayMicroseconds(audioPulseWidthMicros);                 //Waits until the audio has played for the desired period of time

    digitalWrite(AUDIO_RELAY_PIN, LOW);                       //Turns off the audio
    actualAudioOffTimeMicros = micros();                      //Records the time that the audio was turned off.
    digitalWrite(TENS_RELAY_PIN, HIGH);                       //Turns on the TENS relay to allow a pulse to go through shortly hereafter.
    TENSRelayOnTime = micros();                               //Records the time that the TENS relay was turned on
    while(nTotalPulses == prevNTotalPulses){                  //Runs an empty loop while waiting for the optocoupler to be triggered next.  Can't use delay here because we're waiting on an interrupt from the TENS optocoupler
      int q=0;                                                //does nothing, just bides time
      q++;                                                    //does nothing, just bides time
    }
    actualTENSOnTime = risingPulseTimeMicros;                 //Copies the time that the TENS unit fired in microseconds from the ISR function
    delayMicroseconds(500);
    digitalWrite(TENS_RELAY_PIN, LOW);                        //Turns off the TENS relay
    actualAudioPulseWidthMicros = actualAudioOffTimeMicros - actualAudioOnTimeMicros;   //Calculates the amount of time the audio played for
    actualAudioTimeBeforeTENSPulse = actualTENSOnTime - actualAudioOffTimeMicros;       //Calculates the amount of time that passed between the audio ending and TENS pulse arriving
    actualTENSRelayTimeBeforePulse = risingPulseTimeMicros - TENSRelayOnTime;           //Calculates how much empty time there was between the TENS relay opening and a TENS pulse actually going through it.
    printData();


  }

}



void readOptoPin1(){
  readTime_micros = micros();   //Records the time that the TENS pulse was measured.

  lastRisingPulseTimeMicros = risingPulseTimeMicros;    //saves the previous rising pulse time as the last one
  risingPulseTimeMicros = readTime_micros;              //sets the current rising pulse time to the read_time recorded at the beginning of this ISR function

  nTotalPulses++;               //Increments the total number of pulses measure.

}


int first1SecondSync(){
  int numSamples = 35;          //Sets number of desired samples for determining average TENS unit frequency
  int readTimes[numSamples];    //Creates empty array to be used to store sample times
  int pulsePeriodMicros[numSamples];  //Creates empty array to be used to calculate the periods between pulses.
  float pulsePeriodMillis[numSamples];
  int idx = 1;                  //index used later in function.  Starts at 1 so that the 0th element of the pulsePeriodMicros array can be defined later.
  int n_last = 0;               //Used to smooth out initialization of pulsePeriodMicros array calculation
  int avgPulsePeriodMicros;   
  readTimes[0] = micros();
  int funcStartTime = micros();
  digitalWrite(AUDIO_RELAY_PIN, HIGH);                      //Turns on the audio

  Serial.print("\n");


  while (micros()-funcStartTime < 1*1000000){ //run for first 1 second of
    
    if (nTotalPulses!=n_last){   //Checks to see if the total number of pulses has changed.
      Serial.print("NTotalPulses inside sync function: ");
      Serial.print(nTotalPulses);
      Serial.print("\n");
      Serial.print("Millis inside sync loop: ");
      Serial.print((micros()-funcStartTime)/1000.00000);
      Serial.print("ms\n");

      n_last = nTotalPulses;       //Sets the last pulse to the current one
      readTimes[idx] = risingPulseTimeMicros;  //Sets the next readTime in the array to the current time value
      if (idx != 0){
        pulsePeriodMicros[idx]=readTimes[idx] - readTimes[idx-1];   //Calculates the time that passed between pulses and adds it to the array
      }
      else if (idx == 0){
        pulsePeriodMicros[idx]= readTimes[idx] - readTimes[numSamples-1];
      }
      pulsePeriodMillis[idx] = pulsePeriodMicros[idx]/1000.00000;
      idx++;          //Increments the array
      if (idx == numSamples) {    //Resets the array so that the values continuously overwrite. Only the most recent "numSamples"s number of samples are kept
        idx=0;
      }
      //printData();

      int sumPeriods = 0;       //This takes the mean of the pulsePeriodMicros array
      for(int sample : pulsePeriodMicros){
        sumPeriods = sample + sumPeriods;
      }
      avgPulsePeriodMicros = sumPeriods / numSamples;

      Serial.print("Pulse Periods: \n");
      int j=0;
      for (float pulsePeriod : pulsePeriodMillis){
        Serial.print("Pulse period ");
        Serial.print(j);
        Serial.print(": ");
        Serial.print(pulsePeriod);
        Serial.print("ms\n");
        j++;
      }

      Serial.print("Average Pulse Period: ");
      Serial.print(avgPulsePeriodMicros/1000.000000);
      Serial.print("ms\n");

    }

  }
  digitalWrite(AUDIO_RELAY_PIN, LOW);                      //Turns on the audio

  return avgPulsePeriodMicros;
}

void printData(){
  
  Serial.print("Bimodal Frequency [Hz]: ");
  Serial.println(frequency_Hz, 4);

  Serial.print("Time that Audio preceeded TENS pulse [ms]: ");
  Serial.println(actualAudioTimeBeforeTENSPulse/1000.000, 4);

  Serial.print("Audio Pulse Width [ms]: ");
  Serial.println(actualAudioPulseWidthMicros/1000.000, 4);

  // Serial.print("TENS relay on time before pulse [ms]: ");
  // Serial.println(actualTENSRelayTimeBeforePulse/1000.0000, 4);

  // Serial.print("Number of Pulse Reads: ");
  // Serial.print(nTotalPulses);
  // Serial.print("\n");

  Serial.print("\n");
  Serial.print("\n");

}
